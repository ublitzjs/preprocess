## cache can be acquired in libuv worker ONLY in order to synchronize disk usage with NodeJS application. Always when queing caching job I create a cache dummy with status 0

### AST optimization here means a list of structures telling "where is how much of text of what purpose".
But saving "where" for each structure as a uint32_t offset would be too pricy, so instead it holds {uint8_t purpose, uint32_t length} with special alignment of 1 - total of 5 bytes. 
AST optimization IS NOT PERFORMED on templates with "dynamically removed" parts. ONLY FOR INTERPOLATION. When looking for both syntax parts handing was split into finding prefix and figuring out IF PREFIX WAS ACTUALLY PREFIXING SYNTAX, or it was just a coincidence. But here only prefix+interpolation is searched together, so if found - definitely means interpolation.
Optimizations can be acquired in 2 ways.
  1) if you take cache from global map, it may have optimization and may not, though it definitely is cached full in memory. When some task picks cache up, it creates an interpolation level (if streamToFS or streamToJS) with a cache state (in .compile - that's all needed) holding cache, "uint16_t ast_index" meaning which exact index of ast it has processed (but in case 2 below usage slightly different). if cache is not ready yet, 
  2) If task reads a template, but it was specified from js as "optimized". In this case file looks as follows: [uint16_t amountOfOptimizations][many 5-byte structures][file content]. This optimization can be created with .compile js function.

### Memory economy
When template is being cached other tasks may want to subscribe to it. Here std::vector of tasks comes in hand. But storing it in cache itself is harmful (because can't be removed any time soon). Instead there is tbb::concurrent_unordered_map which will store vectors. When cache is created - tasks are queued and vector is deleted.

Also memory and portability issues come when talking about AST_Item.
It should contain uint32_t and bool.
However using #pragma pack(1) creates non-portable to ARM architecture application.
So in cache container instead of saving std::vector I would just store an "8-byte handler class" for a mallocated chunk as 5-byte data array.
This 8-byte handler is just one pointer and plenty methods on referencing data inside of that chunk. 
Now appears the question: "How would I know the size? Where to store it?". 
The size should be stored in cache container as std::atomic<uint8_t> AND IT will be the synchronization mechanism when several threads wait for cache to get optimized.
However to avoid alignment of 8 bytes and stay without any paddings, one byte seems to be needed.
But now let's look at these values of cache container:
    Status status - enum value which actually never needs more than 4 bits
    bool cacheCanHaveAST - 1 bit. // helps with understanding polymorphic caches
    bool fileHasAST - 1 bit. // this one is needed only when caching first time in libuv. By this I decide if I should read file normally or "2 bytes -> 5-byte structs -> contents"
    So it means that they should be combined in 1 byte together and using some math being operated individually.

### Multithreaded AST creation.
Processing functions are usually different, but one thing definitely unites them - parallelized cache handling. Now so that AST exists it is important to understand, that several threads may process same cache in parallel, the result of this processing should not go to waste, unnecessary work should not be done and my C++ app should have mercy on the server, which might choose my app. 
1) Global cache needs to have such statuses: 0 - waiting to be cached by libuv, < 0 - errored, 1 - Cached without ast, 2 - some thread performs ast optimization, 3 - cached and optimized
2) When some task finds cache in state 1 - it creates state 2 and strives to get ast as soon as possible. Only after that it again loops through ast as if nothing happened.
3) (It doesn't apply to templates, which are streamed BECAUSE they are not global and don't suffer from multithreading) 
If some task finds cache in status 2 - it uses a std::atomic from cache itself to make thread sleep and wait.
  Threads will be woken up each time the processing task encounters an interpolation. Why? Other threads will run through recently made changes (which will be saved before using cv to trigger others), and try to find themselves... A JOB (jumpscare). I mean that if at some point task will be required to make a recursive insertion of another template, it would be better for it to start as soon as possible, instead of sleeping. 
  You might have asked: "if sleeping is so bad, why wouldn't you just leave that cache as soon as you found it and go to next task?". I DON'T KNOW, if next task holds different cache. If does - I am lucky. 
  Now you might have thought: "Who would start processing same template 2+ times in a row?". You're probably right, but the fact is that tasks, waiting in a queue, can have different state of completion and different template to be used at that particular moment. So thread shouldn't avoid such templates because it has no guarantees about finding another JOB (jumpscare).
  4) processing task on the stack memory creates std::array<AST_Item, 2> (or something like this).
  As I said before - tasks optimize only completely cached templates for interpolation and non-interpolation.
  In this case non-interpolation can't repeat > 1 time in a row, interpolation can repeat 2+ times in a row.
  After finding "interpolation" each time task locks accessor of cache in global map, pushes new ast to cache, unlocks accessor, notifies others via std::atomic::notify_all. But before it actually increases this std::atomic<uin16_t> to match the already processed amount.
  When task is notified it immediately creates a const_accessor on given cache, reads ast, releases accessor and again start waiting for change in std::atomic. As the old value for atomic it will use current ast_index of state in task.

  5) When processing task finishes optimization, it:
    1. locks global map (which is done usually only when status of cache changes)
    2. sets status to 3
    3. unlocks accessor
    4. again increases std::atomic and uses notify_all
    5. processes all ast as if it was there whole this time.



### Certain functions in js
.cache function
lock accessor in global map and try to get cache
    if does not exist:
        1. check syntax
        2. create cache dummy on the heap and insert in map
        3. create libuv data struct with the pointer to cache and "boolean" if it has "ast" inside
        4. queue libuv to cache
    if exists:
        if status < 0 (ONLY IF IT WAS PROCESSED AND ENDED UP BEING INVALID. ALSO IT IS AUTOMATICALLY REMOVED REALLY FAST, SO CHANCES TO MEET SUCH STATUS ARE LOW):
            unlock accessor
            throw js error and that's all
        if status >= 0
            increase busyLevel by one and that's all


.compile function. first string param - input filename, second boolean - save in global cache whole file, third string? - output, fourth (status)=>void - callback
if output param exists - check if empty. If is - throw js error.
lock accessor in global map and try to get cache
    if does not exist:
        check syntax
        create cache dummy on the heap
        get file descriptor, file size.
        if save
            attach cache dummy to accessor
            if size == 0:
                set status 3
                release accessor
                if output string:
                    open output descriptor
                    write "00" to output
                    close output descriptor
                call js callback and quit
        release accessor
        create task on the heap and insert data above in it.
        create libuv data and insert task in it
        queue libuv to cache for optimization
    if exists:  
        if status < 0: do same as in .cache
        if status 0 (.compile is not meant for this case. Don't create a background for it): 
            1. busyLevel++
            release accessor
            2. create a task struct forOptimization on the heap with cache pointer, save boolean, output as std::string (even empty), and "invalid_file_size" (look below)
            NOTICE!!! I don't get file's size, but initialize it with "invalid_file_size". When time comes to processing, it will mean that cache container has full size and It was initialized in libuv.
            3. find or create a vector in concurrent unordered map for that cache.
            4. push this task to vector and exit.
        if 0 < status (that's still ok if status 1, but IT IS NOT MEANT TO BE USED LIKE THAT):
            if size 0 && output string
               open output descriptor
               write "00" to output
               close output descriptor
               call callback and quit
            1. busyLevel++
            release accessor
            2. same as step 2 above, but init with normal size
            3. queue processing task 



.streamToFS (and almost same in streamToJS). first param - object, second - templates filenames and their state, third - output, fourth - callback
in first param get number "id".
Use id in second param to get template info (tuple)
use first index in tuple to get filename of template
lock global caches map and try to get cache
    if exists:
        if status < 0 : do same as in .cache
        if status 0:
            1. busyLevel++
            2. release accessor
            3. create a task struct forFS with all data from params and invalid_file_data to the state of processing
            4. repeat steps 3,4 from .compile and status 0
        if status > 0;
            busyLevel++
            release accessor
            same as step 3 above but init with normal size. (probably write that in constructor of forFS)
            queue processing task


### Libuv workers

for .cache
get cache pointer
get file descriptor
get file size
if has ast inside:
    if file size < 2 // DEVELOPERS ARE NOT THAT LAZY TO MISS SUCH CASES
        do everything AS IF IT WAS ERROR
    if file size == 2 // file was empty all this time. DEVELOPERS - DON'T DO THIS
        close descriptor
        lock global map
        set status 3
        unlock map
        do step 9 below
    1. create on stack uint16_t and read 2 first chars from file to it - amount of 5-byte structs
    2. set to std::atomic in cache container this amount
    3. malloc 5 * amount of bytes buffer and WITHOUT LOCKING ANY MUTEX set this pointer to cache dummy
    4. read to that pointer from file
    5. malloc(fileSize - 5 * amount) and set that pointer to cache container WITHOUT MUTEXES
    6. read to that pointer from file
    7. lock map -> set status 3 -> unlock map
    8. close descriptor
    9. run through waiting tasks. Queue each to thread pool
if no ast inside
    if fileSize 0: do same as if cache had ast and size was 2 bytes
    mallocate as much as file size is 
    read file to that pointer
    close descriptor
    lock global map
    set status to 1
    unlock map
    find waiting tasks in unordered_map
        if exist: loop through them, and enqueue each to thread pool
if error:
    close descriptor
    lock global map
    delete cache pointer
    loop through waiting tasks
    for each task call tsfn, which as first param to callback passes "{file,status}" js object, release tsfn
    unlock global map


for .compile
get task pointer from libuvDataStruct
get state of current cache
get cache container, file size, file descriptor, already processed size, length of partials from state,
if cache should be cached whole - save param, cache's dummy is in global map (so this is first and last call to libuv)
    if has ast:
        1. create on stack uint16_t and read 2 first chars from file to it - amount of 5-byte structs
        2. set to std::atomic in cache container this amount
        3. malloc 5 * amount of bytes buffer and WITHOUT LOCKING ANY MUTEX set this pointer to cache dummy
        4. read to that pointer from file
        5. malloc(fileSize - 5 * amount) and set that pointer to cache container WITHOUT MUTEXES
        6. read to that pointer from file
        7. lock map -> set status 3 -> unlock map
        8. close descriptor
        9. run through waiting tasks. Queue each to thread pool. quit

## cache can be acquired in libuv worker ONLY in order to synchronize disk usage with NodeJS application. Always when queing caching job I create a cache dummy with status 0

### AST optimization here means a list of structures telling "where is how much of text of what purpose".
But saving "where" for each structure as a uint32_t offset would be too pricy, so instead it holds only uint32_t sizeAndPurpose - two numbers, uint31 and uint1
AST optimization IS NOT PERFORMED on templates with "dynamically removed" parts. ONLY FOR INTERPOLATION. When looking for both syntax parts handing was split into finding prefix and figuring out IF PREFIX WAS ACTUALLY PREFIXING SYNTAX, or it was just a coincidence. But here only prefix+interpolation is searched together, so if found - definitely means interpolation.
Optimizations can be acquired in 2 ways.
  1) if you take cache from global map, it may have optimization and may not, though it definitely is cached full in memory. When some task picks cache up, it creates an interpolation level (if streamToFS or streamToJS) with a cache state (in .compile - that's all needed) holding cache, "uint16_t ast_index" meaning which exact index of ast it has processed (but in case 2 below usage slightly different). if cache is not ready yet, 
  2) If task reads a template, but it was specified from js as "optimized". In this case file looks as follows: [uint16_t amountOfOptimizations][many 32byte ints][file content]. This optimization can be created with .compile js function.

### Memory economy
When template is being cached other tasks may want to subscribe to it. Here std::vector of tasks comes in hand. But storing it in cache itself is harmful (because can't be removed any time soon). Instead there is tbb::concurrent_unordered_map which will store vectors. When cache is created - tasks are queued and vector is deleted.

Also memory and portability issues come when talking about AST_Item.
It should contain uint32_t and bool.
However using #pragma pack(1) creates non-portable to ARM architecture application.
I will create one uint32_t, which with simple math will be used as uint31 and uint1
Now appears the question: "How would I know the size? Where to store it?". 
in cache I will store a helper class - same as std::vector, but I don't need "capacity" and "size". Just reallocation.

The size should be stored in cache container as std::atomic<uint8_t> AND IT will be the synchronization mechanism when several threads wait for cache to get optimized.
However to avoid alignment of 8 bytes and stay without any paddings, one byte seems to be needed.
But now let's look at these values of cache container:
    Status status - enum value which actually never needs more than 4 bits
    uint16_t busyLevel - amount of tasks / "js asked", which gets deleted if 0 - 12 bits as an overrated maximum
    bool cacheCanHaveAST - 1 bit. // helps with understanding polymorphic caches
    bool sourceFileHasAST - 1 bit. // this one is needed only when caching first time in libuv. By this I decide if I should read file normally or "2 bytes -> uint32_t numbers -> contents"
    bool cacheIsGlobal - 1 bit. // Just a sign, created the moment cache is allocated
It may seem at first, that all these can be somehow combined because have clear limits, BUT here comes "thread safety". 
I cannot unite "status" and sourceFileHasAST", because "sourceHasAST" can be read whenever wanted and is READONLY, while for status "caching::dataMap" should always be locked.
 Using logic above, I CAN unite "busyLevel" and "status", because both require map to be locked, and unite "cacheCanHaveAST" with "sourceFileHasAST", because both a readonly booleans

+ .compile processing function would first read/stream the template and find ast and then -> queue libuv to write that all to output. But instead of recreating new input descriptor I would just "move" descriptor to the beginning. On Linux it is "lseek" function.

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

### Recursion levels and states of processing
state - structure, containing most metadata about cache.
However it should not by itself contain a cache pointer.
For example, if .compile is called task needs to process one single cache -> it can be stored in task directly

In streamToFS or streamToJS story is a bit different.
There is a custom array of all used and "to be used" cache pointer as well as their counter of awaiting usages.
It actually is just one pointer of data, which looks like this: [2byte num][8byte pointer]...
All this custom array does is provides helper methods to operate this data as if it was is structures. (alignment is pricy and #pragma packs break ARM architecture)
Its size is stored in "second param" to streamToXX - templatesList array from js. 
When constructing that buffer on the heap its values are zeros and null pointers. Its size == (2+8) * templatesList.Length()
uint2 - amount for cache to definitely live before its removal/deletion. If zero from the beginning and pointer to the right is valid - cache has unlimited usage and should be removed in the end of the function. If was not zero and after some usage WAS SUPPOSED TO BECAME zero (but I leave it as a 1) - gets removed and pointer = nullptr. If that jpreviously removed cache happened to be used again (I would find "1" at the old usages), then I set usages to 0 instead of the amount passed from js.
Each position in that buffer corresponds to position in templatesList. So each time new template recursively needs to be processed - its index is used to lookup from templatesList (number of usages and filename) and put to the same index in custom array.


Also there is a stack of "inclusion levels" for streamToXX.
Whenever new template(s) need(s) to be included - new level is created and pushed to stack.
Level represents one reuseable state and js object/array. 
Levels are "singular" and "plural". Singular holds single js object and plural holds js array of objects + index of array to be accessed at the time.
Each object holds "id" and "keys" - interpolation data.
When the level is entered, I create cache pointer& on the stack and call "getCurrentParams": singular level returns me its only object and plural - object at its index
Then to get cache pointer I use that custom array from before and lookup (it must be initialized at function start) cache pointer from it at given id.

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



for .clearCache
lock map and try to find cache
if not exists: return true
if exists:
    if status 0 (.cache in libuv will take care of that) || --busyLevel: return false
    delete accessor
    delete cache from heap
    return true



.compile function. first string param - input filename, second boolean - save in global cache whole file, third string? - output, fourth (status)=>void - callback
if output param exists - check if empty. If is - throw js error.
lock accessor in global map and try to get cache
    if does not exist:
        check syntax
        create cache dummy on the heap
        get file descriptor, file size.
        if save || fileSize <= maxChunkSize (if !save but fileSize <= maxChunkSize I will in the end of processing delete cache):   
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

### error handling through polymorphism
This is tree of tasks:
            MinBase 
          /         \
forOptimization       Base
                    /      \
                 forJS    forFS

MinBase has "emitError" virtual, which for... classes override.
forOptimization::emitError does this:
    close input descriptor
    call tsfn with first param as status
    release tsfn
    // don't delete cache, because it will be done outside this function
    // output descriptor doesn't exist until the end, so no. it is not here
forFS :
    1. close output descriptor, remove output completely
    2. remove top stack level (because its cache failed.)
    3. run through each stack level from the "new" top (touch only CURRENT template). For each:
        close input descriptor
        get index of current cache from js object in level
        get current cache&
        bool shouldBeDeleted = true;
        if cache is in map:
            lock map
            shouldBeDeleted = !--busyLevel
            if shouldBeDeleted:
                erase accessor
        if shouldBeDeleted : delete cache from heap
        cache = nullptr (it is reference to pointer in custom array)
        remove current level from stack
    4. run through custom array for each pointer:
        if !pointer: continue;
        bool shouldBeDeleted = true;
        if cache is in map:
            lock map
            shouldBeDeleted = !--busyLevel
            if shouldBeDeleted:
                erase accessor
    5. release "params" and "templatesList" js values references
    6. call tsfn with first param like [status, templateName], release tsfn (finalizer should delete the task)
        

forJS // left to write
### Libuv workers

for .cache
get cache pointer
get file descriptor
get file size
if has ast inside:
    if file size < 2 // DEVELOPERS ARE NOT THAT LAZY TO MISS SUCH CASES
        do everything AS IF IT WAS ERROR
    if file size == 2 // file was empty all this time. DEVELOPERS - DON'T DO THIS
        do steps "7 -> 12" below
    1. create on stack uint16_t and read 2 first chars from file to it - amount of ast items
    2. set to std::atomic in cache container this amount
    3. malloc sizeof(uint32_t) * amount of bytes buffer and WITHOUT LOCKING ANY MUTEX set this pointer to cache dummy
    4. read to that pointer from file
    5. malloc(fileSize - sizeof(uint32_t) * amount) and set that pointer to cache container WITHOUT MUTEXES
    6. read to that pointer from file
    7. close descriptor
    8. lock map 
    9. if busyLevel 0 (somebody called clearCache ahead of time): delete cache pointer from map and from heap, exit
    10. set status 3
    11. unlock map
    12. run through waiting tasks (if exist). Queue each to thread pool
if no ast inside
    if fileSize 0: do steps "7 -> 12" above
    mallocate as much as file size is 
    do steps "6 -> 9" above
    set status to 1
    do steps "11, 12" above
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
if cache should be cached whole - save param, cache's dummy is in global map (so this is first and last call to libuv). BY THE WAY, "compile" is meant to be used for files which DEFINITELY have NO AST yet
    1. malloc size of file and set pointer to cache container
    2. write from file to that pointer
    3. close descriptor
    4. lock map -> set status 1 -> unlock map
    5. queue current task
    6. run through waiting tasks. Queue each to thread pool. quit
if cache is not required by user to be cached whole - hence it may be not the first libuv call
    1. leftSize = fileSize - processedSize
    2. maxSizeToRead = std::min(leftSize, maxChunkSize)
    3. sizeToRead = maxSizeToRead - syntaxPartialsSize
    4. processedSize+=sizeToRead
    5. bool firstEntry = cache->pointer (if already exists - not first libuv entry)
    6. if cache container has nullptr: malloc(sizeToRead) and set pointer to container WITHOUT MUTEXES
    7. read file to the "pointer + syntaxPartialsSize" as an offset
    8. if processedSize == fileSize : 
        close input descriptor AND replace it in state to invalid
        if firstEntry (so it is first and last libuv entry -> cache dummy is in map)
            lock accessor in map -> set status 1 -> unlock map
            run through waiting tasks and queue to thread pool.
       else (cache definitely is not global -> needs no thread safety)
            status = 1
    9. queue current task
if error:
    1. close input descriptor
    2. call tsfn with status, release tsfn.
    2. if firstEntry && processedSize == fileSize (cache is in global map)
        create accessor 
        run through waiting tasks, for each call "emitError" function
        erase accessor
    3. delete cache from the heap


for streamToFS
get task pointer from libuvDataStruct
get state from the highest interpolation level
get cache container, input size, input descriptor, output descriptor (!!!), length of partials from state
do steps "1 -> 4" above

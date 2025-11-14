### cache can be acquired in libuv worker ONLY in order to synchronize disk usage with NodeJS application. Always when queing caching job I create a cache dummy (it depends whether it is global or not).



libuv callbacks are different for 3 cases:
  - for .cache from js. Error handling may seem cumbersome, but function .cache is meant to be used when template is valid and can be read to memory.
    1) Inside I get cache data from container (which is dummy)
    2) create input descriptor
    3) allocate memory for the whole file.
    4) read file to memory
    5) notify waiting for the cache tasks. 
  !  In case of failure I
    1) close input descriptor
    2) lock global map with caches. It also means that waiting tasks won't be pushed until cache is actually cleared
    3) run through all waiting tasks. For each I activate tsfn with corresponding status in js callback param and release tsfn. Its finalizer is triggered after all js callbacks have finished. As tsfn is saved in heap-allocated "task", it should be deleted in tsfn finalizer (which won't trigger any error).
    4) delete waiting tasks vector.
    5) delete cache container (which will take care of heap allocated memory for file and heap allocated filename of it)
    6) remove cache pointer from map.

  - for .compile from js. This function is meant to be used when template is checked for the first time on its usability. It won't be able to check already optimized templates (because why???)
    1) get descriptor, cache container, cache size from task container
    2) if save param == true - allocate memory for whole file and read whole file
       if save param == false or string - allocate memory for maxChunkSize and read as much. if string it means that optimization is yet to be created, not use already processed template.
       if save param == string (output file), then it means that resulting AST should be output to different file
         1. read first 2 symbols which in fact represent uint16_t - how many ast items are ahead (not actually up to 2^16, but up to 300 is still possible). 
         2. create 
         1) stream file and output NOWHERE
         2) save AST in some local vector 
         3) open output file and write AST in some specific format to its first symbols
         4) copy input file to output file after AST
         5) do not save to global caches. Let js user decide, how to identify the fact it is optimized (just name it like "output.ast.txt")
  - for streamToFS and streamToJS same libuv but different processing callback through polymorphism.
  



### AST optimization here means a list of structures telling "where is how much of text of what purpose".
But saving "where" for each structure as a uint32_t offset would be too pricy, so instead it holds {uint8_t purpose, uint32_t length} with special alignment of 1 - total of 5 bytes. 
AST optimization IS NOT PERFORMED on templates with "dynamically removed" parts. ONLY FOR INTERPOLATION. When looking for both syntax parts handing was split into finding prefix and figuring out IF PREFIX WAS ACTUALLY PREFIXING SYNTAX, or it was just a coincidence. But here only prefix+interpolation is searched together, so if found - definitely means interpolation.
Optimizations can be acquired in 2 ways.
  1) if you take cache from global map, it may have optimization and may not, though it definitely is cached full in memory. When some task picks cache up, it creates an interpolation level (if streamToFS or streamToJS) with a cache state (in .compile - that's all needed) holding cache, "uint16_t ast_index" meaning which exact index of ast it has processed (but in case 2 below usage slightly different). if cache is not ready yet, 
  2) If task reads a template, but it was specified from js as "optimized". In this case file looks as follows: [uint16_t amountOfOptimizations][many 5-byte structures][file content]. This optimization can be created with .compile js function.

### Memory economy when caching in libuv
When template is being cached other tasks may want to subscribe to it. Here std::vector of tasks comes in hand. But storing it in cache itself is harmful (because can't be removed any time soon). Instead there is tbb::concurrent_unordered_map which will store vectors. When cache is created - tasks are queued and vector is deleted.


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



### Processing flow of certain functions in js
.cache function
lock accessor in global map and try to get cache
    if does not exist:
        1. check syntax
        2. create cache dummy on the heap and insert in map
        3. create libuv data struct with the pointer to cache
        4. queue libuv to cache
    if exists:
        if status < 0 (ONLY IF IT WAS PROCESSED AND ENDED UP BEING INVALID. ALSO IT IS AUTOMATICALLY REMOVED REALLY FAST, SO CHANCES TO MEET SUCH STATUS ARE LOW):
            unlock accessor
            throw js error and that's all
        if status >= 0
            increase busyLevel by one and that's all


.compile function. first string param - input filename, second boolean - save in global cache whole file, third string? - output, fourth (status)=>void - callback
if output exists - check if empty. If is - throw js error.
lock accessor in global map and try to get cache
    if does not exist:
        check syntax
        create cache dummy on the heap
        get file descriptor, file size.
        create task on the heap and insert data above in it.
        create libuv data and insert task in it
        queue libuv to cache for optimization
    if exists:  
        if status < 0: do same as in .cache
        if status 0 (.compile is not meant for this case. Don't create a background for it): 
            1. busyLevel++
            2. create a task struct forOptimization on the heap with cache pointer, save boolean, output as std::string (even empty), and "invalid_file_size" (look below)
            NOTICE!!! I don't get file's size, but initialize it with "invalid_file_size". When time comes to processing, it will mean that cache container has full size and It was initialized in libuv.
            3. find or create a vector in concurrent unordered map for that cache.
            4. push this task to vector and exit.
        if 0 < status (that's still ok if status 1, but IT IS NOT MEANT TO BE USED LIKE THAT):
            if status 3 && output is not string - call callback and quit (still have to write to output)
            1. busyLevel++
            2. same as step 2 above, but init with normal size
            3. queue processing task 

// this part below still needs to be edited. That's my old thought
  - if I call either addon.streamToJS or addon.streamToFS
      1) try to get cache from global.
        if could not - get file descriptor, get file size. 
      1) get file descriptor
      2) get file size
      create task container, emplace state of current template inside with acquired data. 

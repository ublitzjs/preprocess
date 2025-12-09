# TROUBLE WHICH COST ME 1 MONTH
I was analyzing this parallelised tokenizer for over a month with a thought that "accessing js data once I got it is thread safe" - FATAL MISTAKE. V8 moves data inside its memory and it can't be avoided, so decision is either serialize data OR stick to single js thread. I prefer second option. This tokenizer depends on data js passes it, so its amount can't be monitored and running through it is very slow.

## Templates are cached to memory only in libuv workers. 
In js thread I add or remove cache containers, while libuv gets only its pointer, reads file, saves data and proceeds to other work. I don't want to overuse locking mechanisms, so instead of tbb::concurrent_hash_map for caches I put simple std::unordered_map + tbb::spin_mutex for each cache (1-byte and no alignment penalty). Libuv won't add (no need for this) and remove by itself (even if error happened) from map, but instead will use one single ThreadSafeFunction to queue "clearing callback" for certain cache.

For each cache container I will save "union" of uint32 size and single boolean "sourceFileHasAST". Size is null and useless until file is cached, but sourceFileHasAST is useless after file is cached - just use same memory when another doesn't need it. In libuv to determine if file is cached globally or not it is enough to fulfill these requirements: don't be in silentCacheCB (there it is global automatically), save boolean if cache was empty when arrived to libuv (if not - streamed and non-global), if its size (given in state) it smaller than maxChunkSize.

Libuv handles work in this way: queue -> execute in worker thread -> "after callback" on the main thread. When I postpone processing in favour of reading file in libuv, I need to queue request for its handling too. Here comes "after callback". However if used only with uv_work_t no Napi::Env is provided, so Napi::AsyncWorker comes in handy.

As I mentioned in the main headline - I want to avoid serialization. "streamToFS" involves disk writes, so at the first glance it should use libuv. However it REQUIRES COPYING DATA. I can write to disk on the main thread. To prove the point further - who would use it extensively every second? As for me it is a rare to use function, which can sacrifice speed in favour of memory usage + useless overhead (it would slow down main thread in each case).

### AST optimization here means a list of structures telling "where is how much of text of what purpose".
But saving "where" for each structure as a uint32_t offset would be too pricy, so instead it holds only int32_t sizeAndPurpose. if > 0 - just read, < 0 - interpolation
AST optimization IS NOT PERFORMED on templates with "dynamically removed" parts. ONLY FOR INTERPOLATION. When looking for both syntax parts handing was split into finding prefix and figuring out IF PREFIX WAS ACTUALLY PREFIXING SYNTAX, or it was just a coincidence. But here only prefix+interpolation is searched together, so if found - definitely means interpolation.
As processing of template is done only on one thread, there is no need to rush and process whole template, so that waiting threads got no job at all. It can be done gradually: task processes template, finds position length, purpose, insert in vector. If it is interpolation - looks if needs another template. If template - leave cache to be and continue processing. Then if that template needed to be read with libuv - another task picks a job AND if finds that unfinished task - it looks at one boolean "completeAST", loops through data, and, if gets through interpolation - continues processing in the same way. It is a "LAZY PROCESSING" with no rush because of multithreading.

### Memory economy
When template is being cached other tasks may want to subscribe to it. Here std::vector of tasks comes in hand. But storing it in cache itself is harmful (because can't be removed any time soon). Instead there is tbb::concurrent_unordered_map which will store vectors. When cache is created - tasks are queued and vector is deleted.

Also memory and portability issues come when talking about AST_Item.
It should contain uint32_t and bool.
However using #pragma pack(1) creates non-portable to ARM architecture application.
I will create one uint32_t, which with simple math will be used as uint31 and uint1
Now appears the question: "How would I know the size? Where to store it?". 
in cache I will store a helper class - same as std::vector, but I don't need "capacity" and "size". Just reallocation.

let's look at these values of cache container:
    Status status - enum value which actually never needs more than 4 bits
    uint16_t busyLevel - amount of tasks / "js asked", which gets deleted if 0 - 12 bits as an overrated maximum
    bool cacheCanHaveAST - 1 bit. // helps with understanding polymorphic caches
    bool sourceFileHasAST - 1 bit. // this one is needed only when caching first time in libuv. By this I decide if I should read file normally or "2 bytes -> uint32_t numbers -> contents"
    bool cacheIsGlobal - 1 bit. // Just a sign, created the moment cache is allocated
It may seem at first, that all these can be somehow combined because have clear limits, BUT here comes "thread safety". 
I cannot unite "status" and sourceFileHasAST", because "sourceHasAST" can be read whenever wanted and is READONLY, while for status "caching::dataMap" should always be locked.
 Using logic above, I CAN unite "busyLevel" and "status", because both require map to be locked, and unite "cacheCanHaveAST" with "sourceFileHasAST", because both a readonly booleans

+ .compile processing function would first read/stream the template and find ast and then -> queue libuv to write that all to output. But instead of recreating new input descriptor I would just "move" descriptor to the beginning. On Linux it is "lseek" function.

### Recursion levels and states of processing
state - structure, containing most metadata about cache.
However it should not by itself contain a cache pointer.
For example, if .compile is called task needs to process one single cache -> it can be stored in task directly

In streamToFS or streamToJS story is a bit different.
There is a custom array of all used and "to be used" cache pointers as well as their counter of future usages.
It is built like this: (cache*  x amount of caches, 16s x amount of caches)
This is just 8byte aligned buffer, where first cache corresponds to first 16s - signed 16bit. If 16bit == -1 -> it has no limit of usages

Task contains a list of chunks to write, which are determined by AST. Also forJS task has it as a js array

Also there is a "stack" (actually a vector. I will tell why) of "inclusion levels" for streamToXX.
Whenever new template(s) need(s) to be included - new level is created and pushed to "stack".
Level represents one reuseable state and js object/array. 
Levels are "singular" and "plural". Singular holds single js object and plural holds js array of objects + index of array to be accessed at the time.
Each object holds "id" and "keys" - interpolation data.
When the level is entered, I create cache pointer& on the stack memory and call "getCurrentParams": singular level returns me its only object and plural - object at its index
Then to get cache pointer I use that custom array from before and lookup (it must be initialized at function start) cache pointer from it at given id.
If there is a cache pointer then no busyLevel should be increased, but if not - find or create one by itself.


### error handling through polymorphism
This is tree of tasks:
            MinBase 
          /         \
forOptimization       Base
                    /      \
                 forJS    forFS

MinBase has "emitError" virtual, which for... classes override.
If error happens in libuv, it doesn't touch waitingTasks, because they need to run on js thread to successfully terminate. 
When Getting back to js thread I run through waiting tasks and emitError each of them. Then manually delete std::vector waitingTasks (if uvWorkers::forStreaming), replace cache->waitingTasks with null and delete cache container.
No task by itself deletes its last template-cache, only everything before. If some template from before (I mean levels) was processed by another task and was found invalid, it didn't delete the cache and decremented busyLevel. So if found invalid cache -> try to clear it.

### Caches acquiring and deletion
when cache was found in map checking status and busyLevel is very important due to possibility of caching being in libuv. Afterwards on the main thread locking mutex is NOT NEEDED. If it is global in map - no libuv. 

If cache is local and streamed (most likely), using mutex is not needed as well, because no ther task will get same cache container (including during its interaction with libuv). 

If streaming task queues libuv:
    if cache is global:
        create std::vector on the heap, insert itself in it, save in cache->waitingTasks and in uvWorkers::forStreaming
    else:
        just save itself in uvWorkers::forStreaming without any vector

### syntax partials, streamToFS
my very first idea was to create completely parallelized template engine. Dream couldn't come true because of GC data relocation. So it is either copying js data, or going single threaded.
When I chose single thread I thought that streamToFS will write to output on the main thread to avoid copying js data.
But now here is third idea. Everything I write to output from js are strings. js strings are utf16, so just to access them transformation (and copying) is REQUIRED. So in fact I can afford to write data to output in libuv.
I will save something like "custom array" from "Recursion levels" above, where I will save pointer AND booleans on the right instead of 16bit integers. These booleans will indicate whether pointer is "char*" or to "std::string*". This way I can ensure that I don't block event loop and don't copy as much data, as in option 1.

maybeAsyncWrite function in tasks (only streamToFS and streamToJS) pushes pointer to data into some array (js/custom). If it "must" give it to final user / output file OR array is full (determine by length of data or amount of indeces in that array), then:
    if streamToFS:
        queues libuv for write and potential read
        returns "true" to mark async execution
        just quit from task
    if streamToJS:
        call callback with an array of data to js
        as one of params give one callback, which will queue libuv for next read depending on third param of maybeAsyncWrite (if it is forced to flush data)
        determine if js told "I have already used your data"
        
        return "!jsToldIfUsed" || forcedFlush to mark async execution
        if async - just quit
        else - continue


# TROUBLE WHICH COST ME 1 MONTH
I was analyzing this parallelised tokenizer for over a month with a thought that "accessing js data once I got it is thread safe" - FATAL MISTAKE. V8 moves data inside its memory and it can't be avoided, so decision is either serialize data OR stick to single js thread. I prefer second option. This tokenizer depends on data js passes it, so its amount can't be monitored and running through it is very slow.

## Templates are cached to memory only in libuv workers. 
In js thread I add or remove cache containers, while libuv gets only its pointer, reads file, saves data and proceeds to other work. I don't want to overuse locking mechanisms, so instead of tbb::concurrent_hash_map for caches I put simple std::unordered_map + tbb::spin_mutex for each cache (1-byte and no alignment penalty). Libuv won't add (no need for this) and remove by itself (even if error happened) from map, but instead will use one single ThreadSafeFunction to queue "clearing callback" for certain cache.

For each cache container I will save "union" of uint32 size and single boolean "sourceFileHasAST". Size is null and useless until file is cached, but sourceFileHasAST is useless after file is cached - just use same memory when another doesn't need it. In libuv to determine if file is cached globally or not it is enough to fulfill these requirements: don't be in silentCacheCB (there it is global automatically), save boolean if cache was empty when arrived to libuv (if not - streamed and non-global), if its size (given in state) it smaller than maxChunkSize.

Libuv handles work in this way: queue -> execute in worker thread -> "after callback" on the main thread. When I postpone processing in favour of reading file in libuv, I need to queue request for its handling too. Here comes "after callback". However if used only with uv_work_t no Napi::Env is provided, so Napi::AsyncWorker comes in handy.


As I mentioned in the main headline - I want to avoid serialization. "streamToFS" involves disk writes, so at the first glance it should use libuv. However it REQUIRES COPYING DATA. I can write to disk on the main thread. To prove the point further - who would use it extensively every second? As for me it is a rare to use function, which can sacrifice speed in favour of memory usage + useless overhead (it would slow down main thread in each case).

### AST optimization here means a list of structures telling "where is how much of text of what purpose".
But saving "where" for each structure as a uint32_t offset would be too pricy, so instead it holds only uint32_t sizeAndPurpose - two numbers, uint31 and uint1
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

+ in streaming I have recursion levels (look below). They need to have uint16_t index or inclusion instrution. Meanwhile fileSize and processedFileSize won't ever reach 16 Exabytes. So I "packed" all in one uint8_t packed[16]. File size data is accessed infrequently and instruction index is placed with taking alignment into consideration.

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


# TROUBLE WHICH COST ME 1 MONTH
I was analyzing this parallelised tokenizer for over a month with a thought that "accessing js data once I got it is thread safe" - FATAL MISTAKE. V8 moves data inside its memory and it can't be avoided, so decision is either serialize data OR stick to single js thread. I prefer second option. This tokenizer depends on data js passes it, so its amount can't be monitored and running through it is very slow.

## Templates are cached to memory only in libuv workers. 
In js thread I add or remove cache containers, while libuv gets only its pointer, read file, saves data and proceeds to other work. I don't want to overuse locking mechanisms, so instead of tbb::concurrent_hash_map for caches I put simple std::unordered_map + tbb::spin_mutex alongside. Libuv won't add (no need for this) and remove (only if error happened) from map, so mutex should be locked only when reading "status" and "busyLevel" fields. Any parallel access may happen only when libuv is working, so protecting cache more is an overkill.
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

### Transition to caching in libuv from other work
As I pointed at the very top - I create a cache dummy in global map (if cache is not supposed to be streamed). But before template is actually cached its "pointer" is nullptr. So I can reuse it for different purpose. In caching::dataStruct I create "union {char* pointer; streaming::data::MinBase* ownerTask; std::vector<streaming::data::MinBase>* waitingTasks}".
Whenever any task gets to some template and needs it to be cached, it:
    opens descriptor and gets file size. If can be cached whole - saves globally. 
    if cache should be streamed -  creates std::vector on the heap, sets it to cache container, pushes itself to it, sets status 0, unlocks map.

ALSO before each caching procedure would requiring queuing work to libuv. This could lead to libuv overflow or just inefficient usage of libuv.
Now I will create "namespace caching::libuv {struct TaggedPointer{void* ptr; bool isState() which also fixes first bit;}; struct dataStruct {uv_work_t* req; std::queue<TaggedPointer*> templates;}; tbb::mutex mutex; dataStruct* worker;".
+ void* templates - tagged pointers. They point to 2+ aligned structures: "caching::dataStruct | streaming::inclusion::stateStruct". First bit is always zero due to the alignment, so I can use it as a boolean. This way I can cache global templates and STREAMED TEMPLATES WITH STATES in one go.
So whenever task wants to cache a template, it ceates a dummy and a TaggedPointer. If task caches it - set first bit to true. If not - don't touch.
Then lock caching::libuv::mutex, look if worker exists. If not - create data, push tagged pointer to queue, set &data to "dataStruct* worker", unlock mutex, queue libuv.
But this functionality works ONLY FOR CACHING purposes. If I create another libuv worker to write "forFS" data to output - I don't do this.
All this eliminates need to create another "libuv handler function" for .compile, since by using option above one handler can cache streamed and non-streamed templates.
Tagged pointer can lead either to cache container or to streaming::data::MinBase. But libuv only cares about cache containers, states and "task->workerCB()" to queue processing. So MinBase needs to have one more virtual function - "getLastState". This way any task can return essential data.

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


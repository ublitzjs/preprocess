#include "./include/os.hpp"
#include <uv.h>
#include "./include/shared2.hpp"
void libuvWorker::Finalize(uv_work_t*, int){}
void libuvWorker::ExecuteWork(uv_work_t *req){
  libuvWorker::dataStruct& workerData = *static_cast<libuvWorker::dataStruct*>(req->data);
  int descriptor = open(workerData.templateName.c_str(), O_RDONLY);
  if (descriptor == -1) {
    return workerData.sync.set_value(Statuses::NoFile);
  }
  // get file's size
  struct stat inputStats;
  if (fstat(descriptor, &inputStats)) {
    close(descriptor);
    return workerData.sync.set_value(Statuses::CantGetSize);
  }
  uint8_t syntaxPartialsSize = workerData.syntaxStruct->syntaxPartialsSize;
  uint32_t mainChunkSize = (inputStats.st_size <= maxChunkSize || workerData.cacheFullFile) ? inputStats.st_size : maxChunkSize;
  char* const wholeChunk = new char[
    mainChunkSize + syntaxPartialsSize
  ];
  if (!wholeChunk){
    close(descriptor);
    return workerData.sync.set_value(Statuses::CantAllocate);
  }
  if (read(descriptor, wholeChunk, mainChunkSize) == -1) {
    delete[] wholeChunk;
    close(descriptor);
    return workerData.sync.set_value(Statuses::CantRead);
  }
  close(descriptor);
  {
    std::lock_guard<std::mutex> lock(caching::dataMapMutex);
    workerData.cache->Init(workerData.cacheIsTmp, wholeChunk, syntaxPartialsSize, mainChunkSize);
  }
  workerData.sync.set_value(Statuses::Success);
}
void libuvWorker::JSCachingThreadPool(
          bool tmp,
          bool cacheFullFile,
          caching::dataStruct* cacheDummy,
          const syntax::dataStruct* const syntaxStruct,
          std::string templateName
      ){
        libuvWorker::dataStruct *workerData = new libuvWorker::dataStruct(tmp, cacheFullFile, syntaxStruct, cacheDummy, templateName); 
        std::future<libuvWorker::Statuses> await = workerData->sync.get_future();
        workerData->uv_request.data = workerData;
        libuvWorker::QueueWork(&workerData->uv_request);
        await.wait();
        uint8_t status = await.get();
        // tsfn is a caching::emitter given from node::events or tseep
        caching::emitter.BlockingCall([status, templateName](
              Napi::Env env, Napi::Function jsCallback) {
          Napi::Value error = 
            status == Statuses::Success
            ? env.Undefined()
            : (status == Statuses::CantAllocate 
                ? Napi::Error::New(env, "Can't allocate memory for this file").Value()
                : Napi::Error::New(env, "Can't open file").Value()
              );
            jsCallback.Call({Napi::String::New(env, templateName), error});
          });
      }

//void workers::FS::ThreadPool(PatternStruct* patternStruct, Napi::ThreadSafeFunction){
//  using Worker = workers::FS;
//   workerData = workers::FS();
//  //here writeablePointer is set to initialPointer
//  BaseStream* streams = getStreams();
//  Timer timer("Worker function");
//  if(streams->hasFinished()) goto afterProcess;
//	do { // reads chunks
//		
//    //writeablePointer DOESN'T get rewritten on chunk reads
//		streams->readNewChunk();
//
//		do { // checks each chunk
//			if (streams->action == streams->Action::None) {
//        /*In this branch writablePointer DEFINITELY is defined on entry*/
//        char* prefixPointer = streams->chunk_findTemplateSyntax(streams->patternStruct->prefix);
//
//        if(!prefixPointer){
//					uint32_t leftChunkSize = streams->chunk_size - streams->chunk_pointerMovedDistance();
//          uint8_t maxLengthOfTemplateSyntax = streams->patternStruct->prefix.size() + streams->patternStruct->maxParamLength;
//					if (leftChunkSize > maxLengthOfTemplateSyntax)
//            streams->write(streams->chunk_writablePointer, leftChunkSize - maxLengthOfTemplateSyntax);
//					streams->copySyntaxPartials(maxLengthOfTemplateSyntax);
//					break;
//          
//        } else {
//          // prefixPointer can equal writablePointer ONLY if writeable equals currentPointer
//          if(prefixPointer == streams->chunk_writablePointer) streams->chunk_writablePointer = nullptr;
//          /* I don't write chunk here due to POTENTIAL "fake prefixes"*/
//
//          streams->chunk_currentPointer = prefixPointer + streams->patternStruct->prefix.size() ;
//          
//          uint32_t leftChunkSize = streams->chunk_size - streams->chunk_pointerMovedDistance();
//          if(// If there is not enough space left in chunk to get the "action" after "prefix"
//             leftChunkSize < streams->patternStruct->maxParamLength 
//          ){
//            streams->copySyntaxPartials(
//                streams->chunk_size - (prefixPointer - streams->chunk_getInitialPointer())
//            );
//            //Get out and read next chunk
//            break;
//          } else { 
//            // Save it for later
//            char* afterActionPointer = streams->chunk_currentPointer;
//            // these "if" statements DO NOT exit from this current cycle.
//            if(
//              !streams->patternStruct->insertOn.empty() 
//              && !memcmp(
//                streams->chunk_currentPointer,
//                streams->patternStruct->insertOn.data(),
//                streams->patternStruct->insertOn.length()
//              )
//            ){
//              streams->action = streams->Action::Inserting;
//              afterActionPointer+=streams->patternStruct->insertOn.size();
//            } else if(!streams->patternStruct->removeOn.empty() 
//              && !memcmp(
//                streams->chunk_currentPointer,
//                streams->patternStruct->removeOn.data(),
//                streams->patternStruct->removeOn.length()
//              )
//            ){
//              streams->action = streams->Action::Removing;
//              afterActionPointer+=streams->patternStruct->removeOn.size();
//            } else if (streams->patternStruct->removeOn.empty()){
//              streams->action = streams->Action::Removing;
//              afterActionPointer+=streams->patternStruct->removeOn.size();
//            } else if (streams->patternStruct->insertOn.empty()){
//              streams->action = streams->Action::Inserting;
//              afterActionPointer+=streams->patternStruct->insertOn.size();
//            } else {
//              /*current pointer is AFTER the prefix here*/
//              // Prefix is FAKE so I need to mark it as writeable to stream
//              if(!streams->chunk_writablePointer) streams->chunk_writablePointer = prefixPointer;
//              continue;
//            }
//            /* here prefix is NOT fake */
//            if(streams->chunk_writablePointer) {
//              streams->write(
//                  streams->chunk_writablePointer,
//                  streams->chunk_currentPointer - streams->chunk_writablePointer - streams->patternStruct->prefix.size()
//              );
//            }
//            streams->chunk_currentPointer = afterActionPointer;
//            leftChunkSize = streams->chunk_size - streams->chunk_pointerMovedDistance();
//            if(!leftChunkSize) break;
//            uint8_t sufficientEndLength = streams->patternStruct->end.size() + 1 /*one byte before end*/;
//            if(leftChunkSize < sufficientEndLength){
//              streams->copySyntaxPartials(leftChunkSize);
//              break;
//            } else continue;// restart cycle in appropriate branch
//            
//          }
//        }
//      } else if (streams->action == streams->Action::Removing){
//	      char* endPointer = streams->chunk_findTemplateSyntax(streams->patternStruct->end);
//        if(!endPointer){
//          streams->copySyntaxPartials(streams->patternStruct->end.size() - 1);
//          break;
//        } else {
//          streams->action = streams->Action::None;
//          streams->chunk_currentPointer = endPointer + streams->patternStruct->end.size();
//          uint32_t leftChunkSize = streams->chunk_size - streams->chunk_pointerMovedDistance();
//          if(!leftChunkSize) break;
//          else if(leftChunkSize < streams->patternStruct->prefix.size() + streams->patternStruct->maxParamLength){
//            streams->copySyntaxPartials(leftChunkSize);
//            break;
//          } else continue;
//        }
//      } else if (streams->action == streams->Action::Inserting){
//        // chunk_currentPointer DEFINITELY is AFTER action's syntax. It points on insertion indentifier's first byte.
//        uint16_t maxAllowedDistanceToEnd = streams->patternStruct->maxSyntaxPartialsSize;
//	      char* endPointer = streams->chunk_findTemplateSyntax(
//            streams->patternStruct->end,
//            maxAllowedDistanceToEnd
//        );
//        uint32_t leftChunkSize = streams->chunk_size - streams->chunk_pointerMovedDistance();
//        if(!endPointer) {
//          if(leftChunkSize < maxAllowedDistanceToEnd){
//            streams->copySyntaxPartials(leftChunkSize);
//            break;
//          } else {
//            //TODO error!!!
//          }
//        } 
//        /*TODO: handle case, where end-pointer is SAME as currentPointer OR is 256 and more bytes away OR is absent*/
//        streams->insert(streams->chunk_currentPointer, endPointer - streams->chunk_currentPointer);
//        streams->chunk_currentPointer = endPointer + streams->patternStruct->end.size();
//        leftChunkSize = streams->chunk_size - streams->chunk_pointerMovedDistance();
//        if(!leftChunkSize) break;
//        else if(leftChunkSize < streams->patternStruct->prefix.size() + streams->patternStruct->maxParamLength){
//          streams->copySyntaxPartials(leftChunkSize);
//          break;
//        } else continue;
//      }
//    } while(true);
//
//
//		if (streams->hasFinished()) {
//      if(streams->syntaxPartialsSize)
//        streams->write(streams->syntaxPartials, streams->syntaxPartialsSize);
//      
//      streams->flushChunks();
//      break;
//    } else streams->flushChunks();
//
//	} while (true);
//
//afterProcess:
//  delete streams;
//}

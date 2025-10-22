#include <stdlib.h>
#include <stdint.h>
#include <algorithm>
#include <future>
#include <vector>
#include <regex>
#include <thread>
#include "./include/os.hpp"
#include "./include/shared.hpp"
//#include <stringzilla/stringzilla.hpp>
#include <napi.h>

std::vector<PatternStruct>* patterns = new std::vector<PatternStruct>();
uint32_t maxChunkSize = 1024*64;

class JSStream : public BaseStream {
private: 
  Napi::Array queue;
  Napi::Env jsEnv;
public:
  JSStream(const PatternStruct* patternStr, const std::string inputPath, Napi::ThreadSafeFunction tsfn, Napi::Env env) 
    : BaseStream(patternStr, inputPath, tsfn), queue(std::move(Napi::Array::New(env))), jsEnv(env) {}
  void flushChunks() override {
    std::promise<void> promise;
    auto waiter = promise.get_future();
    Napi::Array& queueVar = queue;
    tsfn.BlockingCall(&"", [queueVar, &promise](Napi::Env env, Napi::Function jsCallback, const void*){
      Napi::Function doneFn = Napi::Function::New(env, [
          promiseWhenDone = std::move(promise)
      ](const Napi::CallbackInfo& info) mutable {
          promiseWhenDone.set_value();
          return info.Env().Undefined();
      });
      jsCallback.Call({ queueVar, doneFn });
    });
    waiter.wait();
    queue = Napi::Array::New(jsEnv);
  }
  inline void write(const char* source, uint32_t size) override {
    queue.Set(
      queue.Length(),
      Napi::ArrayBuffer::New(jsEnv, (void*) source, size)
    );
  }
  inline void insert(char* keyword, uint8_t size) override {
    write(keyword, size);
  }
};
class FSStream : public BaseStream {
private:
  std::vector<LightBuffer<const char*>> queue;
  Napi::Object filling;
  int output;
public:
  
  FSStream(const PatternStruct* patternStr, const std::string inputPath, Napi::ThreadSafeFunction tsfn, const std::string outputPath, Napi::Object fillingParam) 
    : BaseStream(patternStr, inputPath, tsfn), filling(fillingParam) {
      queue.reserve(3);
    output = open(outputPath.c_str(), O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    //if(output == -1){
		//	std::cerr << "Couldn't create output file " <<  outputPath;
    //  close(input);
    //  exit(1);
    //}
  }

  void flushChunks() override {
    for(LightBuffer<const char*> buffer: queue){
      ::write(output, buffer.source, buffer.length);
      //if(::write(output, source, size) == -1) {
		  //	printf("Error: unable to write to file.");
		  //	close(input),close(output);
		  //	exit(1);
      //};
    }
    queue.clear();
    tsfn.BlockingCall(&"", [](Napi::Env, Napi::Function jsCallback, const void*){
      jsCallback.Call({});
    });
  }
  inline void write(const char* source, uint32_t size) override {
    queue.emplace_back(source, size);
  }
  inline void insert(char* keyword, uint8_t size) override {
    Napi::Value data = filling.Get(keyword);
    if(data.IsString()) {
      const std::string& string = data.As<Napi::String>().Utf8Value();
      write(string.c_str(), string.size());
    } else if(data.IsArrayBuffer()) {
      Napi::ArrayBuffer buffer = data.As<Napi::ArrayBuffer>();
      write(static_cast<char*>(buffer.Data()), buffer.ByteLength());
    } /*TODO error if ELSE*/
  }
};

namespace exports {
  /**
   *  Each file has different name, and depending on its name module uses different symbols
   * */
  void addPattern(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    Napi::Object params = info[0].As<Napi::Object>();
    const std::string& prefix = params.Get("prefix").As<Napi::String>().Utf8Value();
    const std::string& insertOn = params.Get("insertOn").As<Napi::String>().Utf8Value();
    const std::string& removeOn = params.Get("removeOn").As<Napi::String>().Utf8Value();
    const std::string& end = params.Get("end").As<Napi::String>().Utf8Value();
    const uint8_t maxParamLength = std::max<uint8_t>({
        static_cast<uint8_t>(insertOn.length()),
        static_cast<uint8_t>(removeOn.length())
    });
    const uint8_t maxSyntaxPartialsSizeVar = std::max<int16_t>({
        static_cast<uint8_t>(params.Get("maxInsertKeyLength").As<Napi::Number>().Int32Value() + end.size()),
        static_cast<uint8_t>(prefix.size() + maxParamLength),
    });
    patterns->emplace_back(
        std::regex(params.Get("pattern").As<Napi::String>().Utf8Value()),
        prefix,
        insertOn,
        removeOn,
        end,
        maxParamLength,
        maxSyntaxPartialsSizeVar
    );
  }
  /*
   * This function takes either path to a file, or templates
   * */
  void streamToFS(const Napi::CallbackInfo& info) {
    const PatternStruct* patternStruct = nullptr;
    Napi::String input = info[0].As<Napi::String>();
    for(const PatternStruct& patternStr : *patterns){
      std::cmatch matches;
      if(std::regex_search(
        input.Utf8Value().c_str(),
        matches,
        patternStr.pattern
      )){
        patternStruct = &patternStr;
        break;
      }
    }
    Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(
        info.Env(),
        info[3].As<Napi::Function>(),
        "JSStreaming_Worker",
        0,
        1
    );
    std::thread(
      [
        input
      ](){
        std::cout<<"Hello\n\n\n\nHello\n\n\n";
      }
      //Worker,
      //[
      //  patternStruct,
      //  tsfn,
      //  in = input.Utf8Value(),
      //  out = info[1].As<Napi::String>().Utf8Value(),
      //  params = info[2].As<Napi::Object>()
      //]()->BaseStream*{
      //  FSStream* streams = new FSStream(patternStruct, input.Utf8Value().c_str(), tsfn, out.Utf8Value().c_str(), params);
      //  return streams;
      //}
    ).detach();
  }  
  void streamToJS(const Napi::CallbackInfo& info) {
    const PatternStruct* patternStruct = nullptr;

    for(const PatternStruct& patternStr : *patterns){
      std::cmatch matches;
      if(std::regex_search(
        info[0].As<Napi::String>().Utf8Value().c_str(),
        matches,
        patternStr.pattern
      )){
        patternStruct = &patternStr;
        break;
      }
    }
    Napi::Env env = info.Env();
    Napi::ThreadSafeFunction tsfn = Napi::ThreadSafeFunction::New(
        env,
        info[1].As<Napi::Function>(),
        "JSStreaming_Worker",
        0,
        1
    );
    
    std::thread(
      Worker,
      [patternStruct, env, tsfn, in = info[0].As<Napi::String>().Utf8Value().c_str()]()->BaseStream*{
        JSStream* streams = new JSStream(patternStruct, in, tsfn, env);
        return streams;
      }
    ).detach();
  }
  void setMaxChunk(const Napi::CallbackInfo& info){
    maxChunkSize = info[0].As<Napi::Number>().Int32Value();
  }
}

Napi::Object Init(Napi::Env env, Napi::Object info){
  info.Set("addPattern", Napi::Function::New(env, exports::addPattern));
  info.Set("setMaxChunk", Napi::Function::New(env, exports::setMaxChunk));
  info.Set("streamToFS", Napi::Function::New(env, exports::streamToFS));
  info.Set("streamToJS", Napi::Function::New(env, exports::streamToJS));
  return info;
}

NODE_API_MODULE(addon, Init);

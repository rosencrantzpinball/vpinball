// license:GPLv3+

#include "core/stdafx.h"

#ifdef __EMSCRIPTEN__

#include "utils/BrowserFileStore.h"

#include <emscripten/em_js.h>
#include <emscripten/threading.h>

namespace BrowserFileStore
{

// Requests go one at a time through this control block in the heap, which the worker waits on (Atomics.wait)
enum Slot : int { STATE, OP, PTR_LO, PTR_HI, SIZE, OFFSET_LO, OFFSET_HI, RESULT, READY, N_SLOTS };
enum : int32_t { IDLE, REQUESTED, DONE }; // STATE
enum : int32_t { OP_WRITE = 1, OP_READ }; // OP
enum : int32_t { STARTING, AVAILABLE, UNAVAILABLE }; // READY

alignas(8) static int32_t s_control[N_SLOTS];
static std::mutex s_mutex; // Held for a request, and guards the two below
static uint64_t s_end = 0; // Where the next entry goes in the store's file
static uint64_t s_inUse = 0; // Total size of the entries not released yet

// The worker removes the files that pages which ended left (one another page still has open is locked and stays), then
// opens a file of its own and serves requests. The file never shrinks: that, like growing it, waits for the main thread.
// The worker's code needs no line breaks, as the preprocessor joins it into one line.
EM_JS(void, vpx_browser_file_store_start, (double control), {
   const serve = async function(e) {
      const memory = e.data.memory;
      const c = new Int32Array(memory.buffer, e.data.control, 9);
      const STATE = 0, OP = 1, PTR_LO = 2, PTR_HI = 3, SIZE = 4, OFFSET_LO = 5, OFFSET_HI = 6, RESULT = 7, READY = 8;
      let file;
      try {
         const dir = await navigator.storage.getDirectory();
         try {
            for await (const name of dir.keys())
               if (name.startsWith("vpx-table-files-"))
                  await dir.removeEntry(name).catch(function() {});
         } catch (err) {
         }
         file = await (await dir.getFileHandle(e.data.name, { create: true })).createSyncAccessHandle();
      } catch (err) {
         Atomics.store(c, READY, 2);
         return;
      }
      Atomics.store(c, READY, 1);
      for (;;) {
         let state;
         while ((state = Atomics.load(c, STATE)) !== 1)
            Atomics.wait(c, STATE, state);
         let ok = false;
         try {
            const size = c[SIZE] >>> 0;
            const at = (c[OFFSET_LO] >>> 0) + (c[OFFSET_HI] >>> 0) * 4294967296;
            const data = new Uint8Array(memory.buffer, (c[PTR_LO] >>> 0) + (c[PTR_HI] >>> 0) * 4294967296, size);
            ok = (c[OP] === 1 ? file.write(data, { at: at }) : file.read(data, { at: at })) === size;
         } catch (err) {
         }
         c[RESULT] = ok ? 1 : 0;
         Atomics.store(c, STATE, 2);
         Atomics.notify(c, STATE);
      }
   };
   try {
      const source = "onmessage = " + serve.toString() + ";";
      const worker = new Worker(URL.createObjectURL(new Blob([source], { type: "text/javascript" })));
      const name = "vpx-table-files-" + (self.crypto.randomUUID ? self.crypto.randomUUID() : String(Math.random()).slice(2));
      worker.postMessage({ memory: wasmMemory, control: control, name: name });
   } catch (err) {
      Atomics.store(new Int32Array(wasmMemory.buffer, control, 9), 8, 2);
   }
});

void Start()
{
   assert(emscripten_is_main_browser_thread());
   vpx_browser_file_store_start(static_cast<double>(reinterpret_cast<uintptr_t>(s_control)));
}

bool IsStarting()
{
   return __atomic_load_n(&s_control[READY], __ATOMIC_SEQ_CST) == STARTING;
}

uint64_t GetStoredSize()
{
   const std::lock_guard lock(s_mutex);
   return s_inUse;
}

// The caller holds s_mutex
static bool Request(const int32_t op, void* const ptr, const size_t size, const uint64_t offset)
{
   assert(size <= UINT32_MAX);
   const uint64_t address = reinterpret_cast<uintptr_t>(ptr);
   s_control[OP] = op;
   s_control[PTR_LO] = static_cast<int32_t>(static_cast<uint32_t>(address));
   s_control[PTR_HI] = static_cast<int32_t>(static_cast<uint32_t>(address >> 32));
   s_control[SIZE] = static_cast<int32_t>(static_cast<uint32_t>(size));
   s_control[OFFSET_LO] = static_cast<int32_t>(static_cast<uint32_t>(offset));
   s_control[OFFSET_HI] = static_cast<int32_t>(static_cast<uint32_t>(offset >> 32));
   __atomic_store_n(&s_control[STATE], REQUESTED, __ATOMIC_SEQ_CST);
   emscripten_futex_wake(&s_control[STATE], 1);
   while (__atomic_load_n(&s_control[STATE], __ATOMIC_SEQ_CST) == REQUESTED)
      emscripten_futex_wait(&s_control[STATE], REQUESTED, INFINITY);
   const bool ok = s_control[RESULT] == 1;
   __atomic_store_n(&s_control[STATE], IDLE, __ATOMIC_SEQ_CST);
   return ok;
}

Entry Store(vector<uint8_t>& data)
{
   Entry entry;
   // The main thread would wait on the worker, while a write that makes the file grow waits on the main thread
   if (data.empty() || emscripten_is_main_browser_thread() || __atomic_load_n(&s_control[READY], __ATOMIC_SEQ_CST) != AVAILABLE)
      return entry;
   const std::lock_guard lock(s_mutex);
   if (!Request(OP_WRITE, data.data(), data.size(), s_end))
      return entry;
   entry.offset = s_end;
   entry.size = data.size();
   s_end += entry.size;
   s_inUse += entry.size;
   vector<uint8_t>().swap(data);
   return entry;
}

vector<uint8_t> Read(const Entry& entry)
{
   vector<uint8_t> data(entry.size);
   if (data.empty())
      return data;
   const std::lock_guard lock(s_mutex);
   if (!Request(OP_READ, data.data(), data.size(), entry.offset))
   {
      PLOGE << "Failed to read back " << entry.size << " bytes from the browser's file store";
      vector<uint8_t>().swap(data);
   }
   return data;
}

void Release(Entry& entry)
{
   if (entry.size == 0)
      return;
   const std::lock_guard lock(s_mutex);
   s_inUse -= entry.size;
   entry = Entry();
   if (s_inUse == 0)
      s_end = 0;
}

}

#endif

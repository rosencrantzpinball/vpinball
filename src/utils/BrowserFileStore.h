// license:GPLv3+

#pragma once

#ifdef __EMSCRIPTEN__

// On the web, keeps the table's image and sound files out of the wasm heap, which never gives memory back to the browser,
// so the page holds only the files in use. They are written to one file in the browser's origin private file system
// (OPFS), on disk, by a plain web worker (no wasm instance, so it costs little memory) that owns the file and serves
// synchronous reads and writes straight from and into the heap. Any thread may read, even while the page's main thread
// waits on a lock (a synchronous read of a Blob would wait for the main thread, in Safari). Writes come only from other
// threads while the main thread is free, as a write that makes the file grow waits for it, in Safari. Without OPFS (some
// private browsing modes), nothing is moved.
namespace BrowserFileStore
{
   // Starts the store's worker, which is ready once it has opened its file, shortly after
   void Start();
   // Whether the worker is still opening its file (the main thread must be back in the browser for it to go on)
   bool IsStarting();
   // Total size of the entries in the store
   uint64_t GetStoredSize();

   // A file's data moved to the store
   struct Entry
   {
      uint64_t offset = 0;
      size_t size = 0; // 0 when nothing is stored
   };

   // Moves data to the store: on success, frees it and returns its entry; otherwise (or on the main thread) leaves it
   // and returns an empty entry. The main thread must not wait on the calling thread meanwhile
   Entry Store(vector<uint8_t>& data);
   // Reads an entry's data back (empty if the store failed), from any thread
   vector<uint8_t> Read(const Entry& entry);
   // Gives back an entry's space and empties the entry (the space is used again once nothing is left in the store)
   void Release(Entry& entry);
}

#endif

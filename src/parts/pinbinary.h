// license:GPLv3+

// interface for the PinBinary class.

#pragma once

#include "utils/fileio.h"
#ifdef __EMSCRIPTEN__
#include "utils/BrowserFileStore.h"
#endif

class PinBinary
{
public:
   PinBinary() = default;
#ifdef __EMSCRIPTEN__
   virtual ~PinBinary() { BrowserFileStore::Release(m_stored); }
#else
   virtual ~PinBinary() = default;
#endif

   void Save(IObjectWriter& writer) const;
   void Load(IObjectReader& reader);

   bool ReadFromFile(const std::filesystem::path& filename);
   bool WriteToFile(const std::filesystem::path& filename) const;

#ifdef __EMSCRIPTEN__
   // On the web, a table's image files wait outside the wasm heap until needed (BrowserFileStore)
   void MoveToBrowser();
   void RestoreBuffer(); // Brings m_buffer back from the browser for good, if it was moved there
   size_t GetSize() const;
   // m_buffer or, if it was moved to the browser, a copy read back into readBack (for a one-off use)
   const vector<uint8_t>& GetData(vector<uint8_t>& readBack) const;
#else
   size_t GetSize() const { return m_buffer.size(); }
   const vector<uint8_t>& GetData(vector<uint8_t>&) const { return m_buffer; }
#endif

   string m_name;
   std::filesystem::path m_path;
   vector<uint8_t> m_buffer;

#ifdef __EMSCRIPTEN__
private:
   BrowserFileStore::Entry m_stored;
   mutable std::mutex m_storedMutex;
#endif
};

class PinFont final : public PinBinary
{
};

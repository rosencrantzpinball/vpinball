// license:GPLv3+

#include "core/stdafx.h"
#include "parts/pinbinary.h"

bool PinBinary::ReadFromFile(const std::filesystem::path& filename)
{
#ifdef __EMSCRIPTEN__
   {
      const std::lock_guard lock(m_storedMutex);
      BrowserFileStore::Release(m_stored);
   }
#endif
   m_buffer = read_file(filename);
   m_path = filename;
   m_name = TitleFromFilename(filename);
   return true;
}

bool PinBinary::WriteToFile(const std::filesystem::path& filename) const
{
   vector<uint8_t> readBack;
   write_file(filename, GetData(readBack));
   return true;
}

void PinBinary::Save(IObjectWriter& writer) const
{
   vector<uint8_t> readBack;
   const vector<uint8_t>& data = GetData(readBack);
   writer.WriteString(FID(NAME), m_name);
   writer.WriteString(FID(PATH), PathToUTF8(m_path));
   writer.WriteInt(FID(SIZE), static_cast<int>(data.size()));
   writer.WriteRaw(FID(DATA), data.data(), static_cast<int>(data.size()));
   writer.EndObject();
}

#ifdef __EMSCRIPTEN__
void PinBinary::MoveToBrowser()
{
   const std::lock_guard lock(m_storedMutex);
   if (m_stored.size == 0)
      m_stored = BrowserFileStore::Store(m_buffer);
}

void PinBinary::RestoreBuffer()
{
   const std::lock_guard lock(m_storedMutex);
   if (m_stored.size)
   {
      m_buffer = BrowserFileStore::Read(m_stored);
      BrowserFileStore::Release(m_stored);
   }
}

size_t PinBinary::GetSize() const
{
   const std::lock_guard lock(m_storedMutex);
   return m_stored.size ? m_stored.size : m_buffer.size();
}

const vector<uint8_t>& PinBinary::GetData(vector<uint8_t>& readBack) const
{
   const std::lock_guard lock(m_storedMutex);
   if (m_stored.size == 0)
      return m_buffer;
   readBack = BrowserFileStore::Read(m_stored);
   return readBack;
}
#endif

void PinBinary::Load(IObjectReader& reader)
{
   reader.AsObject(
      [this](int tag, IObjectReader& reader)
      {
         switch (tag)
         {
         case FID(NAME): m_name = reader.AsString(); break;
         case FID(PATH): m_path = PathFromUTF8(reader.AsString()); break;
         case FID(SIZE): m_buffer.resize(reader.AsInt()); break;
         // Size must come before data, otherwise our structure won't be allocated
         case FID(DATA): reader.AsRaw(m_buffer.data(), static_cast<int>(m_buffer.size())); break;
         }
         return true;
      });
}



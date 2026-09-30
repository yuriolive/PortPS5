// IFileWriter implementation: writes relinker output files.
// Subsystem: relinker io. Output is buffered by the stream, so write errors
// (disk full) only appear at flush; each overload therefore close()s before
// testing the stream. Throws Domain::RelinkerException. No shared state.

#include <io/FileWriter.hpp>
#include <domain/Types.hpp>
#include <fstream>

namespace Io {

void FileWriter::Write(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f)
        throw Domain::RelinkerException("Cannot open output file: " + path);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    // Output is buffered: a full disk or I/O error may only surface when the
    // buffer is flushed. close() flushes and sets failbit on error, whereas
    // the destructor would swallow it and report a truncated file as success.
    f.close();
    if (!f)
        throw Domain::RelinkerException("Failed to write file: " + path);
}

void FileWriter::Write(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f)
        throw Domain::RelinkerException("Cannot open output file: " + path);
    f << content;
    f.close();  // See the binary overload: surface deferred flush failures.
    if (!f)
        throw Domain::RelinkerException("Failed to write file: " + path);
}

}

// Regression tests for Io::FileWriter deferred-flush error reporting.
// Ported from AnyPS5 42594eec and converted to GoogleTest.
//
// Invariant: FileWriter::Write buffers output, so a write error (disk full)
// only surfaces at flush time. The writer must close() the stream before
// checking it so the failure is reported instead of being swallowed by the
// destructor. The failure case needs a device whose writes always fail
// (/dev/full), so it is Linux-only and skipped elsewhere; the success cases run
// on every platform. Single-threaded.
#include <io/FileWriter.hpp>
#include <domain/Types.hpp>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

std::filesystem::path TempFile(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

// Invariant: a successful write round-trips exactly (binary overload).
TEST(FileWriter, BinaryRoundTrip) {
    const auto path = TempFile("portps5_filewriter_bin.tmp");
    const std::vector<std::uint8_t> data = {0, 1, 2, 0xff, '\n', '\r', 0};
    Io::FileWriter().Write(path.string(), data);
    std::ifstream in(path, std::ios::binary);
    const std::vector<std::uint8_t> got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(got, data);
    in.close();
    std::filesystem::remove(path);
}

// Invariant: a successful write round-trips exactly (text overload).
TEST(FileWriter, TextRoundTrip) {
    const auto path = TempFile("portps5_filewriter_txt.tmp");
    Io::FileWriter().Write(path.string(), std::string("hello"));
    std::ifstream in(path);
    std::string got;
    std::getline(in, got);
    EXPECT_EQ(got, "hello");
    in.close();
    std::filesystem::remove(path);
}

// Invariant: an unopenable output path is reported as an error.
TEST(FileWriter, UnopenablePathThrows) {
    const auto path = TempFile("portps5_no_such_dir") / "x" / "out.bin";
    EXPECT_THROW(Io::FileWriter().Write(path.string(), std::vector<std::uint8_t>{1}), Domain::RelinkerException);
}

// Small (fits the stream buffer, so only close() can notice) and large (forces
// an in-write flush) payloads must both report failure on /dev/full.
template <typename T>
void ExpectFullDeviceFailure(const T& data) {
    Io::FileWriter writer;
    EXPECT_NO_THROW(writer.Write("/dev/null", data));
    try {
        writer.Write("/dev/full", data);
        FAIL() << "write to /dev/full reported success";
    } catch (const Domain::RelinkerException& e) {
        EXPECT_THAT(e.what(), testing::HasSubstr("Failed to write file: /dev/full"));
    }
}

TEST(FileWriter, BinaryWriteFailureReported) {
#ifndef __linux__
    GTEST_SKIP() << "needs /dev/full (Linux only)";
#else
    for (std::size_t n : {std::size_t{3}, std::size_t{65536}}) ExpectFullDeviceFailure(std::vector<std::uint8_t>(n, 0x41));
#endif
}

TEST(FileWriter, TextWriteFailureReported) {
#ifndef __linux__
    GTEST_SKIP() << "needs /dev/full (Linux only)";
#else
    for (std::size_t n : {std::size_t{3}, std::size_t{65536}}) ExpectFullDeviceFailure(std::string(n, 'A'));
#endif
}

}  // namespace

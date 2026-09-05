#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "fsx.h"
#include "httpx.h"

namespace
{
namespace fs = std::filesystem;

void Write(const fs::path& path, const std::string& value)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << value;
}

TEST(SecurityRegression, RejectsRequestInjectionBeforeTransport)
{
    int calls = 0;
    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.transport = [&](const httpx::Request&, const httpx::ClientOptions&)
    {
        ++calls;
        httpx::Result<httpx::Response> result;
        result.ok = true;
        result.value.status_code = 200;
        return result;
    };
    httpx::Request valid;
    valid.url = "http://localhost/ok";
    valid.body = std::string("a\0\r\nb", 5);
    std::vector<httpx::Request> invalid;
    for (const std::string& value :
         std::vector<std::string>{"x\r\nInjected: yes", "x\ny", std::string("x\0y", 3), std::string("x\x7f", 2)})
    {
        auto request = valid;
        request.headers = {{"X-Test", value}};
        invalid.push_back(request);
    }
    for (const std::string name : {"Bad Name", "Bad:Name", "", "X\r\nInjected"})
    {
        auto request = valid;
        request.headers = {{name, "value"}};
        invalid.push_back(request);
    }
    for (const httpx::HeaderList& headers :
         {httpx::HeaderList{{"Content-Length", "0"}},
          httpx::HeaderList{{"Content-Length", "5"}, {"content-length", "5"}},
          httpx::HeaderList{{"Content-Length", "+5"}}, httpx::HeaderList{{"Transfer-Encoding", "chunked"}},
          httpx::HeaderList{{"Content-Length", "5"}, {"Transfer-Encoding", "chunked"}}})
    {
        auto request = valid;
        request.headers = headers;
        invalid.push_back(request);
    }
    for (int field = 0; field < 3; ++field)
    {
        auto request = valid;
        request.body.clear();
        httpx::MultipartPart part{"name", "file.txt", "text/plain", "data"};
        if (field == 0)
            part.name = "name\r\nInjected: yes";
        if (field == 1)
            part.filename = "file\nInjected: yes";
        if (field == 2)
            part.content_type = "text/plain\r\nInjected: yes";
        request.multipart.push_back(part);
        invalid.push_back(request);
    }
    for (const auto& request : invalid)
    {
        const auto result = httpx::Client(options).Send(request);
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(result.error.kind, httpx::ErrorKind::InvalidArgument);
    }
    EXPECT_EQ(calls, 0);
    valid.headers = {{"Content-Length", "5"}, {"X-Tab", "a\tb"}};
    EXPECT_TRUE(httpx::Client(options).Send(valid).ok);
    EXPECT_EQ(calls, 1);
    options.user_agent = "agent\r\nInjected: yes";
    EXPECT_FALSE(httpx::Client(options).Send(valid).ok);
    EXPECT_EQ(calls, 1);
}

TEST(SecurityRegression, RejectsRawUrlWhitespaceAndControls)
{
    httpx::ClientOptions options;
    options.use_proxy_from_environment = false;
    options.transport = [](const httpx::Request&, const httpx::ClientOptions&)
    {
        httpx::Result<httpx::Response> result;
        result.ok = true;
        return result;
    };
    for (const std::string& suffix : std::vector<std::string>{"/a b", "/a\r\nX: y", "/a\tb", std::string("/a\0b", 4)})
    {
        const auto result = httpx::Client(options).Get("http://localhost" + suffix);
        EXPECT_FALSE(result.ok);
        EXPECT_EQ(result.error.kind, httpx::ErrorKind::InvalidUrl);
    }
    EXPECT_TRUE(httpx::Client(options).Get("http://localhost/a%20b").ok);
}

TEST(SecurityRegression, LinkedEntriesCannotEscapeWalkSyncCopyOrArchive)
{
    const auto root = fs::current_path() / "security-link-regression";
    fs::remove_all(root);
    Write(root / "outside" / "sentinel", "keep");
    fs::create_directories(root / "src");
    fs::create_directories(root / "dst");
    std::error_code ec;
    fs::create_symlink(root / "outside" / "sentinel", root / "src" / "link", ec);
    if (ec)
    {
#ifdef _WIN32
        GTEST_SKIP() << "Windows symbolic-link privilege unavailable: " << ec.message();
#else
        FAIL() << ec.message();
#endif
    }
    EXPECT_FALSE(fsx::WalkDirectory((root / "src").string()).ok);
    EXPECT_FALSE(fsx::BuildDirectoryDiff((root / "src").string(), (root / "dst").string(), true).ok);
    EXPECT_FALSE(fsx::CreateArchive((root / "src").string(), (root / "bad.tar").string()).ok);
    fsx::BatchPlan copy;
    copy.AddCopyTree((root / "src").string(), (root / "dst").string());
    EXPECT_FALSE(fsx::Run(copy).ok);
    fs::remove(root / "src" / "link");
    Write(root / "src" / "link" / "sentinel", "overwrite");
    fs::create_directory_symlink(root / "outside", root / "dst" / "link", ec);
    ASSERT_FALSE(ec) << ec.message();
    EXPECT_FALSE(fsx::Run(fsx::BuildSyncPlan((root / "src").string(), (root / "dst").string(), true)).ok);
    EXPECT_FALSE(fsx::Run(copy).ok);
    std::ifstream in(root / "outside" / "sentinel");
    std::string text;
    in >> text;
    EXPECT_EQ(text, "keep");
    fs::remove_all(root);
}
} // namespace

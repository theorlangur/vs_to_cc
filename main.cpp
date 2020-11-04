#include <filesystem>
#include <fstream>
#include <string_view>
#include <iostream>
#include "json.hpp"

namespace nl = nlohmann;
namespace fs = std::filesystem;

bool utf16_to_ascii(char *pBuf, size_t n, size_t offset)
{
    size_t i = offset;
    size_t j = 0;
    while((i < n) && *(uint16_t*)(pBuf + i))
    {
        if (pBuf[i] && pBuf[i + 1])
            return false;//unexpected;

        pBuf[j] = pBuf[i];
        i += 2;
        ++j;
    }
    if (j && pBuf[j - 1] == '\r') --j;
    pBuf[j] = 0;
    return true;
}

char* prepare_buffer(char *pBuf, size_t n, fs::path const& cl_cmd)
{
  if ((uint8_t)pBuf[0] == 0xff && (uint8_t)pBuf[1] == 0xfe) {
    // convert utf-16 into ascii
    if (!utf16_to_ascii(pBuf, n, 2)) {
      std::string err("Could not convert from utf16 to ascii for ");
      err += cl_cmd.string();
      throw std::runtime_error(err);
    }
  } else if ((uint8_t)pBuf[0] == 0xef && (uint8_t)pBuf[1] == 0xbb &&
             (uint8_t)pBuf[1] == 0xbf) {
    pBuf += 3;
  }else
  {
    size_t off = pBuf[0] == 0 ? 1 : 0;
    if (!utf16_to_ascii(pBuf, n, off)) {
      std::string err("Could not convert from utf16 to ascii for ");
      err += cl_cmd.string();
      throw std::runtime_error(err);
    }
  }
  return pBuf;
}

std::string to_lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {return tolower(c); });
    return s;
}

std::string find_real_name(fs::path p, std::string search)
{
    std::string lower = to_lower(search);
    for (auto& x : fs::directory_iterator(p))
    {
        if (to_lower(x.path().filename().string()) == lower)
            return x.path().filename().string();
    }
    return search;
}

fs::path to_real_path(fs::path p)
{
    if (!p.is_absolute())
        return p;

    auto i = p.begin();
    fs::path res(to_lower(i->string()));
    ++i;
    res /= *i;
    for (++i; i != p.end(); ++i)
    {
        res /= find_real_name(res, i->string());
    }
    res = res.lexically_normal();
    return res;
}

nl::json getEntry(fs::path const& cl_cmd)
{
    nl::json res;
    std::ifstream f(cl_cmd);
    if (f)
    {
        char buf[2048];
        std::fill_n(buf, sizeof(buf), 0);
        if (f.getline(buf, sizeof(buf)))
        {
            char *pBuf = prepare_buffer(buf, sizeof(buf), cl_cmd);
            if (pBuf[0] == '^')
            {
              std::string_view file(pBuf + 1);
              auto slash = file.find_last_of('\\');
              if (slash != file.npos) {
                std::string_view dir(file.data(), slash + 1);
                char cmd[2048];
				std::fill_n(cmd, sizeof(cmd), 0);
                if (f.getline(cmd, sizeof(cmd))) {
                  char *pCmd = prepare_buffer(cmd, sizeof(cmd), cl_cmd);
                  std::string_view c(pCmd);
                  if (c.find_first_of("/c") == 0) {
                    res["file"] = to_real_path(file).string();
                    res["directory"] = to_real_path(dir).string();
                    res["command"] = c; // as-is
                  }
                }
              }
            }
        }
    }
    return res;
}

struct Options
{
    std::string prefixOptions;

};

nl::json createCompileCommands(fs::path const& scan_base, Options const& opt)
{
    nl::json res;
    for(auto& p : fs::recursive_directory_iterator(scan_base))
    {
        if (p.is_regular_file() && p.path().extension() == ".tlog")
        {
            if (p.path().filename().string().find("CL.command") == 0)
            {
                //let's get info
                nl::json item = getEntry(p.path());
                if (item.is_object())
                {
                    std::string cmd = item["command"];
                    if (!opt.prefixOptions.empty())
                      cmd.insert(0, opt.prefixOptions);

                    cmd.insert(0, "clang-cl.exe ");
                    item["command"] = cmd;

                    res.push_back(item);
                }
            }
        }
    }

    return res;
}

int main(int argc, char *argv[])
{
    Options opts;
    fs::path dir;
    fs::path to;
    bool show_help = false;
    for(int i = 0; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--dir")
        {
            ++i;
            dir = argv[i];
        }else if (arg == "--to")
        {
            ++i;
            to = argv[i];
        }else if (arg == "--opt")
        {
            ++i;
            opts.prefixOptions += argv[i];
            opts.prefixOptions += ' ';
        }else if (arg == "--help")
            show_help = true;
    }

    if (dir.empty() || to.empty())
        show_help = true;

    if (show_help)
    {
        std::cout << "Usage: \n"
                  << "vs_to_cc --dir <path-to-win-build-directory> --to <file-to-save-json> [--opt \"opts to insert at the beginning\"]\n";
        return 0;
    }

    try
    {
      std::ofstream res(to);
      res << std::setw(4) << createCompileCommands(dir, opts);
    }catch(std::exception const &e)
    {
        std::cerr << "Failed to create compile commands.\n" << e.what() << "\n";
    }
    return 0;
}
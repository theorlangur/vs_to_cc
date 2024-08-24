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

std::string to_upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {return toupper(c); });
    return s;
}

std::string find_real_name(fs::path p, std::string search)
{
    std::error_code err;
    fs::path cmp_against = p / search;
    for (auto& x : fs::directory_iterator(p, fs::directory_options::skip_permission_denied))
    {
        fs::path xp = p / x;
        if (fs::equivalent(xp, cmp_against, err))
	{
	    //try if a directory iterator can be created
	    try
	    {
		if (x.is_directory())
		{
		    auto iter = fs::directory_iterator(xp);
		    (void)iter->exists();
		}
	    }catch(...)
	    {
		continue;
	    }
            return x.path().filename().string();
	}
    }
    return search;
}

fs::path to_real_path(fs::path p, bool disk_upper)
{
    if (!p.is_absolute())
        return p;

    auto i = p.begin();
    fs::path res(disk_upper ? to_upper(i->string()) : to_lower(i->string()));
    ++i;
    res /= *i;
    for (++i; i != p.end(); ++i)
    {
        res /= find_real_name(res, i->string());
    }
    res = res.lexically_normal();
    return res;
}

nl::json getEntry(fs::path const& cl_cmd, std::ifstream &f, bool disk_upper, bool rev, bool verbose)
{
	nl::json res;
	char buf[2048];
	std::fill_n(buf, sizeof(buf), 0);
	if (f.getline(buf, sizeof(buf)))
	{
		char* pBuf = prepare_buffer(buf, sizeof(buf), cl_cmd);
		if (pBuf[0] == '^')
		{
			std::string_view file(pBuf + 1);
			auto slash = file.find_last_of('\\');
			if (slash != file.npos) {
				std::string_view dir(file.data(), slash + 1);
				char cmd[2048];
				std::fill_n(cmd, sizeof(cmd), 0);
				if (f.getline(cmd, sizeof(cmd))) {
					char* pCmd = prepare_buffer(cmd, sizeof(cmd), cl_cmd);
					std::string_view c(pCmd);
					if (c.find_first_of("/c") == 0) {
						std::string fstr = to_real_path(file, disk_upper).string();
						if (rev)
							std::replace(fstr.begin(), fstr.end(), '\\', '/');

						std::string dstr = to_real_path(dir, disk_upper).string();
						if (rev)
							std::replace(dstr.begin(), dstr.end(), '\\', '/');
						res["file"] = fstr;
						res["directory"] = dstr;
						{
						    char *pCmdCorrection = pCmd;
						    while(*pCmdCorrection)
						    {
							if ((*pCmdCorrection >= 'A' && *pCmdCorrection <= 'Z')
							    && (*(pCmdCorrection+1) == ':')
							    && (*(pCmdCorrection+2) == '\\')
							    )
							{
							    if (disk_upper)
								*pCmdCorrection = toupper(*pCmdCorrection);
							    else
								*pCmdCorrection = tolower(*pCmdCorrection);
							    pCmdCorrection += 3;
							}else
							    ++pCmdCorrection;
						    }
						}
						res["command"] = c; // as-is
					}
					else if (verbose)
						std::cout << "2nd line doesn't start with '/c'. unexpected.\n";
				}
				else if (verbose)
					std::cout << "Couldn't read 2nd line with compile command from " << cl_cmd << "\n";
			}
			else if (verbose)
				std::cout << "1st line: coudln't find '\\' and determine the directory\n";
		}
		else if (verbose)
			std::cout << "1st line doesn't start with '^'\n";
	}
	else if (verbose)
		std::cout << "Couldn't read 1st line from " << cl_cmd << "\n";
	return res;
}

struct Options
{
    std::string prefixOptions;
    bool disk_upper = false;
    bool revert = false;
    bool verbose = false;
};

void getAllEntries(fs::path const& cl_cmd, nl::json &res, Options const& opt)
{
    std::ifstream f(cl_cmd);
    if (f)
    {
        nl::json item;
        while ((item = getEntry(cl_cmd, f, opt.disk_upper, opt.revert, opt.verbose)).is_object())
        {
			std::string cmd = item["command"];
			if (!opt.prefixOptions.empty())
				cmd.insert(0, opt.prefixOptions);

			cmd.insert(0, "clang-cl.exe ");
			item["command"] = cmd;

			res.push_back(item);
        }
    }
    else if (opt.verbose)
        std::cout << "Failed to open file " << cl_cmd << "\n";
}

nl::json createCompileCommands(fs::path const& scan_base, Options const& opt)
{
    nl::json res;
    for(auto& p : fs::recursive_directory_iterator(scan_base, fs::directory_options::skip_permission_denied))
    {
        if (p.is_regular_file() && p.path().extension() == ".tlog")
        {
            if (p.path().filename().string().find("CL.command") == 0)
            {
                //let's get info
                if (opt.verbose)
                    std::cout << "Found CL.command file at " << p.path() << "\n";
                getAllEntries(p.path(), res, opt);
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
        }else if (arg == "--revert")
            opts.revert = true;
        else if (arg == "--disk-up")
            opts.disk_upper = true;
        else if (arg == "--verbose")
            opts.verbose = true;
        else if (arg == "--help")
            show_help = true;
    }

    if (dir.empty() || to.empty())
        show_help = true;

    if (show_help)
    {
        std::cout << "Usage: \n"
                  << "vs_to_cc --dir <path-to-win-build-directory> --to <file-to-save-json> [--verbose] [--revert] [--disk-up] [--opt \"opts to insert at the beginning\"]\n";
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

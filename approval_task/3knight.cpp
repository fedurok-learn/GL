#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <chrono>
#include <type_traits>


#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

namespace fs = std::filesystem;

/**
 * Parse command line args like this:
 * ./knight -la ../ -> std::vector { "./knight", "-l", "-a", "../" }
 * ./knight -l -a -> std::vector { "./knight", "-l", "-a" }
 */
std::vector<std::string> parse_args(int argc, char **argv) {
    std::vector<std::string> args { { argv[0] } };
    for (int i = 1; i < argc; ++i) {
        std::string cur { argv[i] };
        if (cur[0] == '-') {
            for (int j = 1; j < cur.size(); ++j) {
                args.push_back( { cur[j] } );
            }
        } else {
            args.push_back(cur);
        }
    }

    return args;
}

/**
 * ls-specific data info
 * Could show only one directory. Directory, not file.
 */
struct lsdata {
    bool l_flag;
    bool a_flag;
    std::string dirname {'.'};
    
    static lsdata fromRawArgs(const std::vector<std::string>& args) {
        lsdata data;
        for (int i = 1; i < args.size(); ++i) {
            if (args[i] == "l") {
                data.l_flag = true;
            } else if (args[i] == "a") {
                data.a_flag = true;
            } else {
                data.dirname = args[i];
            }
        }

        return data;
    }
};

std::string permissions(const fs::path& file) {
    auto status = fs::status(file);
    std::ostringstream s;
    auto perms = status.permissions();

    if (fs::is_regular_file(status)) s << '-';
    else if (fs::is_directory(status)) s << 'd';
    else if (fs::is_block_file(status)) s << 'b';
    else if (fs::is_character_file(status)) s << 'c';
    else if (fs::is_fifo(status)) s << 'p';
    else if (fs::is_socket(status)) s << 's';
    else if (fs::is_symlink(status)) s << 'l';

    s << ((perms & fs::perms::owner_read) != fs::perms::none ? "r" : "-")
      << ((perms & fs::perms::owner_write) != fs::perms::none ? "w" : "-")
      << ((perms & fs::perms::owner_exec) != fs::perms::none ? "x" : "-")
      << ((perms & fs::perms::group_read) != fs::perms::none ? "r" : "-")
      << ((perms & fs::perms::group_write) != fs::perms::none ? "w" : "-")
      << ((perms & fs::perms::group_exec) != fs::perms::none ? "x" : "-")
      << ((perms & fs::perms::others_read) != fs::perms::none ? "r" : "-")
      << ((perms & fs::perms::others_write) != fs::perms::none ? "w" : "-")
      << ((perms & fs::perms::others_exec) != fs::perms::none ? "x" : "-");
 
    return s.str();
}

struct unix_only_stats { 
    using tstring = decltype(std::put_time((struct tm*) nullptr, (char*)nullptr));

    tstring time;
    std::string owner, group;

    unix_only_stats(tstring time, std::string owner, std::string group)
        : time { time }, owner { owner }, group { group } { }

    unix_only_stats() = default;
};

unix_only_stats get_fstat(const fs::path& file) {
    // this part is not crossplatform, because c++ haven't standardized it yet,
    // to be added in c++20 
    struct stat stat_buf;
    stat(file.string().c_str(), &stat_buf);

    auto modification_time = std::put_time(std::localtime(&stat_buf.st_mtime), "%b %d %H:%M");
    
    auto *pw = getpwuid(stat_buf.st_uid);
    auto  *gr = getgrgid(stat_buf.st_gid);

    if (pw == NULL || gr == NULL) {
        return {};
    }

    return unix_only_stats {
        modification_time, 
        std::string ( pw->pw_name ),
        std::string ( gr->gr_name )
    };
}

auto get_file_size(const fs::path& file) {
    if (!fs::is_directory(file)) {
        return fs::file_size(file);
    } else {
        std::uintmax_t size = 0;
        for (const auto& subfile : fs::directory_iterator { file }) {
            size += get_file_size(subfile.path().string());
        }
        return size;
    }
}

void display(const lsdata& params) {
    fs::path dir { params.dirname };
    for (const auto& file : fs::directory_iterator { dir }) {
        std::string name { fs::proximate(file.path(), dir) };
     
        if (name[0] == '.' && !params.a_flag) continue;
        if (!params.l_flag) {
            std::cout << name << ' ';
            continue;
        }

        auto perms = permissions(file);
        auto link_count = fs::hard_link_count(file);
        auto fsize = get_file_size(file);
        auto [ftime, fowner, fgroup] = get_fstat(file);

        std::cout << perms << ' ' << link_count;
        std::cout << ' ' << fowner << ' ' << fgroup;
        std::cout << ' ' << fsize << ' ' << ftime << ' ' << name <<  '\n';
    }
    std::cout << '\n';
}

int main(int argc, char **argv) {
    auto args = parse_args(argc, argv);
    auto data = lsdata::fromRawArgs(args);
    display(data);
    
    return 0;
}

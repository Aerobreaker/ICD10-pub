/****************************************************************************************************************
* STL includes
****************************************************************************************************************/
#include <iostream>
#include <string>
#include <tuple>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <thread>
#include <utility>

/****************************************************************************************************************
* vcpkg includes
****************************************************************************************************************/
#include <curl/curl.h>
#include <libzippp/libzippp.h>

/****************************************************************************************************************
* Local includes
****************************************************************************************************************/
#include "ArgParser.hpp"

/*
* TODO:
* 1. Switch to using the code file over the orders file?
* 2. A GUI maybe?  Probably with wxwidgets.
*/

/*
* vcpkg dependencies:
* curl
* libzippp
* vcpkg install curl:x86-windows curl:x86-windows-static curl:x64-windows curl:x64-windows-static libzippp:x86-windows libzippp:x86-windows-static libzippp:x64-windows libzippp:x64-windows-static
*/

/*
* NOTES:
* 1. When using static vcpkg libraries, the .vcxproj file needs to be edited to include the following lines within <PropertyGroup Label="Globals"> ... </PropertyGroup>:
*        <VcpkgTriplet Condition="'$(Platform)'=='Win32'">x86-windows-static</VcpkgTriplet>
*        <VcpkgTriplet Condition="'$(Platform)'=='x64'">x64-windows-static</VcpkgTriplet>
*    Otherwise, vcpkg will use the non-static triplet
* 2. When using the static version of libcurl, the following libraries need to be included manually:
*        Ws2_32.lib
*        Wldap32.lib
*        Crypt32.lib
*    These can be included by adding the following lines to {vcpkg_dir}\installed\x64-windows-static\include\curl\curl.h and {vcpkg_dir}\installed\x86-windows-static\include\curl\curl.h:
*        #pragma comment(lib, "Ws2_32.lib")
*        #pragma comment(lib, "Wldap32.lib")
*        #pragma comment(lib, "Crypt32.lib")
*/



using namespace std;


/****************************************************************************************************************
* Compile-time constants
****************************************************************************************************************/
constexpr char DEF_PATH[] = "."; // The default path to use for storing files downloaded and generated

// Visual studio doesn't parse the variable comment correctly if you have // in the following string.  So interrupt the // with "", which get interpreted such that the string continues uninterrupted
constexpr char CMS_BASE_URL[20] = "https:/""/www.cms.gov"; // The base of the ICD-10 URLs.  The cms.gov links are all relational, so store the base for ease of generating a full link given the end

constexpr char CMS_MAIN_URL[23] = "/medicare/coding/icd10"; // The end portion of the main ICD-10 URL

/******************************************************************************************************************
 * META-COMMENT:                                                                                                  *
 * It would probably be better to use icd10cm_codes_yyyy.txt instead of icd10cm_order, but the procedure says use *
 * icd10cm_order, and I haven't verified that there are no differences                                            *
 ******************************************************************************************************************/
constexpr char ORDER_BASE[15] = "icd10cm_order_"; // The base name for the order codes file

constexpr unsigned char IND_CHARS_PER_LINE = 100; // Decimal and non-decimal files are about 100 characters per code

constexpr unsigned char COMB_CHARS_PER_LINE = 200; // Combined is about 200 per code

constexpr unsigned char CODES_CHARS_PER_LINE = 240; // ICD-10 codes file has about one hipaa code per 240 characters

constexpr int ZIP_FILE_SIZE = 3145728; // 2.5 MiB

constexpr int ORDER_FILE_SIZE = 15728640; // 15 MiB

/****************************************************************************************************************
* Structs & enums
****************************************************************************************************************/
// Conainer for code, decimal code, and description for ICD-10 code
struct ICDCode {
    string code;
    string dec_code;
    string desc;
};

// Main program output code enumerator
enum OutputCode : int {
    ok,
    easyhandle_init,
    cms_get_failed,
    icd10_get_failed,
    zip_get_failed,
    icd10_find_failed,
    zip_find_failed,
    extract_file_failed,
};

// This is just a package to hold all of the information to be passed between functions.
// All of the main functions take a ProgramState by reference
struct ProgramState {
    CURL *easyhandle = nullptr; // The CURL easy handle for CURL queries
    bool disp {}; // Flag to turn on or off writing output to stdout
    string dest_path {}; // The path into which to place generated and downloaded files.  Default is DEF_PATH
    string cms_base {}; // The base URL for the cms.gov website.  Default is CMS_BASE_URL
    string cms_url {}; // The relational URL for the ICD-10 page on the cms.gov website.  Default is CMS_MAIN_URL
    string icd10_url {}; // The relational URL for the current most recent tabular order ICD-10 code page
    string zip_url {}; // The relational URL for the link to the tabular order zip file
    string zip_fname {}; // The filename of the current tabular order zip file
    string zip_file {}; // The current tabular order zip file (raw data)
    string order_file {}; // The order codes file from the current zip file
    string dec_codes {}; // Output format decimal codes file
    string ndec_codes {}; // Output format non-decimal codes file
    string comb_codes {}; // Output format combined (decimal and non-decimal) codes file
    string year {}; // The year of the most recent ICD-10 codes
    string working_data {}; // Scratch string for loading web pages into
    int outp = OutputCode::ok; // Current output code for the program
};

/****************************************************************************************************************
* Predeclared support functions (and one main function)
****************************************************************************************************************/
// Initialize a CURL easy handle
bool init_easy_handle(ProgramState &state);

// Load the zip file from fspath into state.zip_file.  Get the year from parser, if it was found; if not, derive from fspath
bool load_zip_file(ProgramState &state, const ArgParser &parser, const filesystem::path &fspath);

// Load the text file from fspath into file
bool load_text_file(string &file, const filesystem::path &fspath);

// Load the files specified in parser into state.dec_codes, state.ndec codes, and state.comb codes if possible.
// Return 0 on success
// Return 1 if dec codes can't be loaded
// Return 2 if non-dec codes can't be loaded
// Return 3 if comb codes can't be loaded
char load_go_files(ProgramState &state, const ArgParser &parser);

// Take the url from base and strip the relational URL into url
// For example:
// string a="https://www.cms.gov/medicare/coding/icd10", b{};
// parse_url(a, b);
// a == "https://www.cms.gov";
// b == "/medicare/coding/icd10";
bool parse_url(string &base, string &url);

// Main work function used by the program.  Take state and compress dec_codes, ndec_codes, and comb_codes into zip files
bool work(ProgramState &state);

/****************************************************************************************************************
* Small support functions (~1-liners)
****************************************************************************************************************/
// Convert a string to all lower case
void to_lower(string &input) { for (char &it : input) it = tolower(it); }

// Compare ICDCodes.  Return a < b
bool comp_icdcode(const ICDCode &a, const ICDCode &b) { return a.code < b.code; }

// Safe cast to throw an error when causing an overflow by casting to a smaller type
template <typename To, typename From>
To safe_cast(const From &value) {
    if (value != static_cast<From>(static_cast<To>(value))) throw overflow_error("casting to smaller type causes overflow");
    return static_cast<To>(value);
}

/****************************************************************************************************************
* Entry point (int main)
****************************************************************************************************************/
// Entry point for the program.  Return the OutputCode from ProgramState.outp
int main(int argc, char *argv[]) {
    // Main function

    // Create an ArgParser for parsing command-line arguments
    ArgParser parser(vector<pair<string, string>>({{"p", "path"}, {"y", "year"}, {"f", "zip-file"}, {"i", "icd10-url"}, {"z", "zip-url"}, {"o", "order-file"}, {"d", "decimal-file"}, {"n", "non-decimal-file"}, {"c", "combined-file"}, {"u", "cms-url"}}));
    parser.add_token("?", "help", false);
    parser.add_token("q", "quiet", false);
    parser.parse(argc, argv);
    // Display usage if the help token is found
    if (parser.found("help")) {
        // Get the filename from argv[0] and clean it up.  That way if the file is renamed, it displays the right filename.
        filesystem::path cur_fil(argv[0]);
        string cur_fname = cur_fil.filename().string();
        if (cur_fname[0] == '"') cur_fname = cur_fname.substr(1);
        if (cur_fname.back() == '"') cur_fname.pop_back();
        cout << endl << "ICD-10 codes update file generator:" << endl;
        cout << endl;
        cout << "Attempts to get the latest ICD-10 code information from the Centers for Medicare & Medicaid Services website, format it for importing into Sunquest, and compress it for delivery to sites." << endl;
        cout << endl;
        cout << cur_fname << " [[/p] Destination] [[/y] Year] [[/f] Zip file] [[/i] ICD-10 URL] [[/z] Zip URL] [[/o] Order file] [[/d] Decimal file [/n] Non-decimal file [/c] Combined file] [[/u] CMS URL] [/q]" << endl;
        cout << endl;
        cout << cur_fname << " /?" << endl;
        cout << endl;
        cout << "  /p --path              Specifies the directory for the generated files to be written to.  If a file is" << endl;
        cout << "                         specified, the parent directory will be used.  If a zip file is specified, it is" << endl;
        cout << "                         assumed to be the ICD-10 code source file, unless the zip file is specified with /f." << endl;
        cout << "  /y --year              Specifies the year that the ICD-10 codes apply for." << endl;
        cout << "  /f --zip-file          Specifies a local file as the source for ICD-10 codes." << endl;
        cout << "  /i --icd10-url         Specifies the URL to search for the ICD-10 code tabular order source file." << endl;
        cout << "  /z --zip-url           Specifies the URL of the ICD-10 code tabular order source file." << endl;
        cout << "  /o --order-file        Specifies a local file as the extracted tabular order ICD-10 codes." << endl;
        cout << "  /d --decimal-file      Specifies a local file which contains Sunquest formatted ICD-10 codes in decimal" << endl;
        cout << "                         format.  Must be used with / n and / c." << endl;
        cout << "  /n --non-decimal-file  Specifies a local file which contains Sunquest formatted ICD-10 codes in non-decimal" << endl;
        cout << "                         format.  Must be used with /d and /c." << endl;
        cout << "  /c --combined-file     Specifies a local file which contains Sunquest formatted ICD-10 codes in both decimal" << endl;
        cout << "                         and non-decimal format.  Must be used with /d and /n." << endl;
        cout << "  /u --cms-url           Specifies the URL to begin searching for ICD-10 codes." << endl;
        cout << "  /q --quiet             Suppress console output." << endl;
        cout << "  /? --help              Displays this help file." << endl;
        cout << endl;

        return OutputCode::ok;
    }

    /************************************************************************************************************
    * Argument validation
    ************************************************************************************************************/
    // Create the ProgramState to pass information around by reference
    ProgramState state;
    state.disp = !parser.found("quiet");
    if (state.disp) cout << endl << "ICD-10 codes update file generator:" << endl << endl;
    if (parser.found("path")) {
        state.dest_path = move(parser.get_value("path"));
        filesystem::path fspath(state.dest_path);
        if (!filesystem::is_directory(state.dest_path)) {
            if (fspath.has_parent_path()) {
                state.dest_path = move(fspath.parent_path().string());
                if (!parser.found("zip-file") && fspath.extension() == ".zip") {
                    if (!load_zip_file(state, parser, fspath)) {
                        state.zip_fname.clear();
                        state.zip_file.clear();
                        state.year.clear();
                    }
                }
                // No need to re-check if a zip file was specified - if we're here, they specified a file.  If it wasn't a zip file or they gave us
                // the zip-file parameter, still display this message.  It doesn't actually matter if we tried to load the file
                if (state.disp && state.zip_file.empty()) cout << "Could not load zip file specified in output path.  Ignoring specified file..." << endl;
            }
        }
        if (state.dest_path.empty()) {
            if (state.disp) cout << "Could not locate output path provided.  Defaulting to current path..." << endl;
            state.dest_path = DEF_PATH;
        }
    } else {
        if (state.disp) cout << "Could not detect provided output path.  Defaulting to current path..." << endl;
        state.dest_path = DEF_PATH;
    }
    if (parser.found("cms-url")) {
        state.cms_base = move(parser.get_value("cms-url"));
        if (!parse_url(state.cms_base, state.cms_url)) state.cms_base.clear();
        if (state.cms_base.empty() && state.cms_url.empty()) {
            if (state.disp) cout << "Could not determine CMS url from provided url.  Defaulting to \"" << CMS_BASE_URL << CMS_MAIN_URL << "\"..." << endl;
            state.cms_base = CMS_BASE_URL;
            state.cms_url = CMS_MAIN_URL;
        } else if (!state.cms_url.empty() && state.cms_base.empty()) {
            // This case shouldn't be possible, I don't think, but handle it anyway
            if (state.disp) cout << "Could not parse CMS url properly.  Ignoring bad parameter.  Defaulting to \"" << CMS_BASE_URL << CMS_MAIN_URL << "\"..." << endl;
            state.cms_base = CMS_BASE_URL;
            state.cms_url = CMS_MAIN_URL;
        }
        // If cms_base isn't empty but cms_utl is, it's a valid URL - no need to handle that case
        // We're also not validating that the URL provided is good; assume it's so if it could be parsed
    } else {
        if (state.disp) cout << "Could not detect provided CMS url.  Defaulting to \"" << CMS_BASE_URL << CMS_MAIN_URL << "\"..." << endl;
        state.cms_base = CMS_BASE_URL;
        state.cms_url = CMS_MAIN_URL;
    }
    if (parser.found("icd10-url")) {
        string base_url = move(parser.get_value("icd10-url"));
        if (!parse_url(base_url, state.icd10_url)) state.icd10_url = "";
        if (!state.icd10_url.empty()) {
            if (parser.found("cms-url")) {
                string expected_base {state.cms_base}, base_url_copy {base_url};
                to_lower(base_url_copy);
                to_lower(expected_base);
                if (base_url_copy != expected_base) {
                    if (state.disp) cout << "Base URL mismatch in ICD-10 url.  Expected: \"" << state.cms_base << "\" received: \"" << base_url << "\"" << endl << "Ignoring bad parameter..." << endl;
                    state.icd10_url.clear();
                } else {
                    state.icd10_url = state.cms_base + state.icd10_url;
                }
            } else {
                state.cms_base = base_url;
                state.icd10_url = base_url + state.icd10_url;
            }
        }
    }
    if (parser.found("zip-url")) {
        string base_url = move(parser.get_value("zip-url"));
        if (!parse_url(base_url, state.zip_url)) state.zip_url = "";
        if (!state.zip_url.empty()) {
            if (parser.found("cms-url")) {
                string expected_base {state.cms_base}, base_url_copy {base_url};
                to_lower(base_url_copy);
                to_lower(expected_base);
                if (base_url_copy != expected_base) {
                    if (state.disp) cout << "Base URL mismatch in zip url.  Expected: \"" << state.cms_base << "\" received: \"" << base_url << "\"." << endl << "Ignoring bad parameter..." << endl;
                    state.zip_url.clear();
                } else {
                    state.zip_url = base_url + state.zip_url;
                }
            } else {
                state.cms_base = base_url;
                state.zip_url = base_url + state.zip_url;
            }
        }
    }
    if (parser.found("zip-file")) {
        state.zip_fname = parser.get_value("zip-file");
        filesystem::path zippath(state.zip_fname);
        if (filesystem::is_regular_file(zippath) && zippath.extension() == ".zip") {
            state.zip_fname = move(zippath.filename().string());
            if (!load_zip_file(state, parser, zippath)) {
                state.zip_fname.clear();
                state.zip_file.clear();
                state.year.clear();
            }
        } else {
            state.zip_fname.clear();
        }
        if (state.disp && state.zip_file.empty()) cout << "Could not load zip file specified.  Ignoring specified file..." << endl;
    }
    if (parser.found("year")) state.year = move(parser.get_value("year"));
    if (parser.found("order-file")) {
        string order_fname = move(parser.get_value("order-file"));
        filesystem::path fspath(order_fname);
        if (filesystem::is_regular_file(fspath) && fspath.extension() == ".txt") {
            if (!load_text_file(state.order_file, fspath)) {
                state.order_file.clear();
            }
        }
        if (state.order_file.empty()) {
            if (state.disp) cout << "Could not load order codes file specified.  Ignoring specified file..." << endl;
        } else if (state.year.empty()) {
            if (state.disp) cout << "Year not found.  Getting year from order filename..." << endl;
            state.year = order_fname.substr(order_fname.rfind(".txt") - 4, 4);
            if (state.disp) cout << "Using year " << state.year << "..." << endl;
        }
    }
    if (parser.found("decimal-file") && parser.found("non-decimal-file") && parser.found("combined-file")) {
        char res = load_go_files(state, parser);
        if (res) {
            if (state.disp) cout << "Encountered an error loading ";
            if (res == 1) {
                if (state.disp) cout << "decimal file (\"" << move(parser.get_value("decimal-file")) << "\").";
            } else if (res == 2) {
                if (state.disp) cout << "non-decimal file (\"" << move(parser.get_value("non-decimal-file")) << "\").";
            } else if (res == 3) {
                if (state.disp) cout << "combined file (\"" << move(parser.get_value("combined-file")) << "\").";
            }
            if (state.disp) cout << "  Ignoring specified .go files..." << endl;
            state.dec_codes.clear();
            state.ndec_codes.clear();
            state.comb_codes.clear();
        } else {
            if (state.year.empty()) {
                if (state.disp) cout << "Year not found.  Attempting to get year from .go filenames..." << endl;
                string dec_file = parser.get_value("decimal-file");
                string ndec_file = parser.get_value("non-decimal-file");
                string comb_file = parser.get_value("combined-file");
                string dec_year = dec_file.substr(dec_file.rfind(".go") - 4, 4);
                string ndec_year = ndec_file.substr(ndec_file.rfind(".go") - 4, 4);
                string comb_year = comb_file.substr(comb_file.rfind(".go") - 4, 4);
                if (dec_year != ndec_year || dec_year != comb_year) {
                    if (state.disp) cout << "Year mismatch.  Found " << dec_year << ", " << ndec_year << ", and " << comb_year << ".  Ignoring .go files..." << endl;
                    state.dec_codes.clear();
                    state.ndec_codes.clear();
                    state.comb_codes.clear();
                } else {
                    state.year = move(dec_year);
                    if (state.disp) cout << "Using year " << state.year << "..." << endl;
                }
            }
        }
    } else if (parser.found("decimal-file") || parser.found("non-decimal-file") || parser.found("combined-file")) {
        if (state.disp) cout << "Cannot load any .go files unless all 3 are loaded.  Ignoring specified .go files..." << endl;
    }

    /************************************************************************************************************
    * Start of main routine
    ************************************************************************************************************/
    if (state.dest_path.back() != filesystem::path::preferred_separator) state.dest_path.push_back(filesystem::path::preferred_separator);

    // Global CURL init result
    const CURLcode res = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (init_easy_handle(state)) {
        // Put the work into a separate function so it can return early and we can stil clean up afterwards
        work(state);
        curl_easy_cleanup(state.easyhandle);
    } else {
        cerr << "Could not acquire curl easy handle!" << endl;
        state.outp = OutputCode::easyhandle_init;
    }
    curl_global_cleanup();
    return state.outp;
}

/****************************************************************************************************************
* Support funcctions
****************************************************************************************************************/
// Receive data from a web page into userdata
size_t receive_data(char *data, size_t size, size_t nmemb, string *userdata) {
    // Support function

    size_t received_size = size * nmemb;

    // We need to specify the length using nmemb to copy null characters from the web page
    userdata->append(data, nmemb);

    return received_size;
}

bool init_easy_handle(ProgramState &state) {
    // Support function
    state.easyhandle = curl_easy_init();
    if (state.easyhandle) {
        curl_easy_setopt(state.easyhandle, CURLOPT_NOPROGRESS);
        curl_easy_setopt(state.easyhandle, CURLOPT_WRITEFUNCTION, receive_data);
    } else {
        return false;
    }
    return true;
}

// Uncompress the file fname from the zip file held in data into outp
bool uncompress_data(const string &data, const string &fname, string &outp) {
    // Support function
    using namespace libzippp;
    ZipArchive *zip_arch = ZipArchive::fromBuffer(data.c_str(), safe_cast<uint32_t>(data.length()));
    if (zip_arch != nullptr) {
        ZipEntry zip_fil;
        for (const ZipEntry &it : zip_arch->getEntries()) {
            string name = it.getName();
            to_lower(name);
            if (name.ends_with(fname)) {
                zip_fil = it;
                break;
            }
        }
        if (zip_fil.isNull() || !zip_fil.isFile()) {
            return false;
        }
        outp = move(zip_fil.readAsText());
        return true;
    }
    return false;
}

// Currently broken
void compress_data2(const string &data, string fname, string ext, char *outp, uint32_t starting_len) {
    using namespace libzippp;
    string fil_name = fname + ext;
    ZipArchive *zip_arch = ZipArchive::fromBuffer(outp, starting_len, ZipArchive::NEW);
    if (zip_arch != nullptr) {
        if (zip_arch->addData(fil_name, data.c_str(), data.length())) {
            ZipEntry zip_fil = zip_arch->getEntry(fil_name);
            zip_fil.setCompressionEnabled(true);
        }
        zip_arch->close();
    }
}

// Store data into a file named fname+ext, then compress the file into base_path+fname+".zip"
void compress_data(const string &data, string &base_path, string fname, string ext) {
    // Support function
    using namespace libzippp;
    string zip_name = base_path + fname + ".zip", fil_name = fname + ext;
    ZipArchive zip_arch(zip_name);
    if (zip_arch.open(ZipArchive::NEW)) {
        if (zip_arch.addData(fil_name, data.c_str(), data.length())) {
            ZipEntry zip_fil = zip_arch.getEntry(fil_name);
            zip_fil.setCompressionEnabled(true);
        }
        zip_arch.close();
    }
}

bool load_zip_file(ProgramState &state, const ArgParser &parser, const filesystem::path &fspath) {
    // Support function
    state.zip_fname = move(fspath.filename().string());
    if (!parser.found("year")) state.year = state.zip_fname.substr(0, 4);
    // Open a stream to the zip file for input in binary mode.  Seek to the end immediately after opening
    ifstream zip_fil(fspath, ios::binary | ios::in | ios::ate);
    if (!zip_fil) {
        return false;
    }
    // Get the character count using tellg
    streampos num_char = zip_fil.tellg();
    // Get a pointer to the raw buffer
    streambuf *zip_buf = zip_fil.rdbuf();
    // Seek the buffer back to the beginning
    zip_buf->pubseekoff(0, ios::beg);
    char *raw_data = new char[num_char];
    // No need to shrink to fit later because we WILL be using all of this space
    state.zip_file.reserve(num_char);
    // Yes zip_fil is an ifstream but using stream-based input formats the inputs which is NOT desired when reading raw
    // data for a zip file.  Use sgetn on the buffer instead.  Use the character count fetched earlier to determine how
    // many characters to get.
    zip_buf->sgetn(raw_data, num_char);
    state.zip_file.assign(raw_data, num_char);
    // zip_fil will be closed automatically by the destructor
    return true;
}

bool load_text_file(string &file, const filesystem::path &fspath) {
    // Support function
    /*
    * Just like load_zip_file, we don't want to do normal stream-based input because we're trying to load the whole file
    * into memory.  Most of the function looks almost identical to load_zip_file for that reason.
    */

    // Open a stream to the zip file for input.  Seek to the end immediately after opening
    ifstream fil(fspath, ios::in | ios::ate);
    if (!fil) {
        return false;
    }
    streampos num_char = fil.tellg();
    streambuf *buf = fil.rdbuf();
    buf->pubseekoff(0, ios::beg);
    char *raw_data = new char[num_char];
    // No need to shrink to fit later because we WILL be using all of this space
    file.reserve(num_char);
    buf->sgetn(raw_data, num_char);
    file.assign(raw_data, num_char);
    return true;
}

char load_go_files(ProgramState &state, const ArgParser &parser) {
    // Support function
    using namespace filesystem;

    string dec_fname = move(parser.get_value("decimal-file"));
    path decpath(dec_fname);
    if (!is_regular_file(decpath) || decpath.extension() != ".go") return 1;
    if (!load_text_file(state.dec_codes, decpath)) return 1;

    string ndec_fname = move(parser.get_value("non-decimal-file"));
    path ndecpath(ndec_fname);
    if (!is_regular_file(ndecpath) || ndecpath.extension() != ".go") return 2;
    if (!load_text_file(state.ndec_codes, ndecpath)) return 2;

    string comb_fname = move(parser.get_value("combined-file"));
    path combpath(comb_fname);
    if (!is_regular_file(combpath) || combpath.extension() != ".go") return 3;
    if (!load_text_file(state.comb_codes, combpath)) return 3;

    return 0;
}

bool parse_url(string &base, string &url) {
    // Support function

    constexpr char http_base[5] = "http";
    constexpr char http_end[4] = "://";
    char offs = 4; // Number of characters in the http base (4 for http, 5 for https)
    size_t strlen = base.size();
    for (size_t i = 0; i < strlen; i++) {
        if (i > static_cast<size_t>(2 + offs)) {
            /* http://
            *      ^ This is the character at position offs
            *  https://
            *         ^ This here is position offs+2*/
            // Get the position of the next / after http:// or https://
            size_t base_end = base.find('/', i);
            if (base_end != string::npos) {
                url = base.substr(base_end);
                base = base.substr(0, base_end);
            }
            return true;
        } else if (i > 3) {
            if (tolower(base[i]) != http_end[i - offs]) return false;
        } else {
            if (tolower(base[i]) != http_base[i]) return false;
            // "http" ends at position 3.  If everything matches up to position 3 and position 4 is 's',
            // it means the URL starts with "https", which is fine
            if ((i == 3) && (tolower(base[4] == 's'))) {
                i++;
                offs++;
            }
        }
    }
    return false;
}

void parse_codes(const string &data, vector<ICDCode> &codes) {
    // Support function

    ICDCode code;
    char hipaa = 0;
    size_t col = 6, data_len = data.length();
    constexpr char codelen = 8; // Max length of an ICD-10 code
    constexpr char deccodelen = 9; // Max length of a decimal format ICD-10 code
    constexpr char desclen = 100; // Max length of a long description

    // Estimate roughly CODES_CHARS_PER_LINE characters per line; reserve the ram to minimize reallocations
    codes.reserve(data_len / CODES_CHARS_PER_LINE);
    code.code.reserve(codelen);
    code.dec_code.reserve(deccodelen);
    code.desc.reserve(desclen);

    for (size_t i = 5; ++i < data_len; col++) {
        if (col >= 6 && col < 14) {
            if (data[i] == ' ') {
                // Next column we care about is 14 so jump to 13
                // Use += for i because it doesn't reset each line
                i += 13 - col;
                // Use += for col for consistency
                col += 13 - col;
            } else {
                // If we've made it to column 9 without seeing a space, the code is long enoug for a decimal
                if (col == 9) code.dec_code.push_back('.');
                code.code.push_back(data[i]);
                code.dec_code.push_back(data[i]);
            }
        } else if (col == 14) {
            hipaa = data[i];
            // Next column we care about is 77 to jump to 76
            i += 62;
            col += 62;
        } else if (hipaa == '1' && col >= 77) {
            if (data[i] == '\r' || data[i] == '\n') {
                // Hit line end - remove trailing spaces
                while (code.desc.back() == ' ') { code.desc.pop_back(); }
            } else {
                code.desc.push_back(data[i]);
            }
        }
        // *nix lines end with \n.  Mac & windows both start with \r.  Take em both!
        if (data[i] == '\r' || data[i] == '\n') {
            // But if we're on windows we need to drop the \n so check for that and skip the next char if it's \n.
            if (data[i] == '\r' && data[i + 1] == '\n') { i++; }
            // We don't care about the first few columns so jump ahead to where we do care
            col = 5;
            i += 6;
            code.code.shrink_to_fit();
            code.dec_code.shrink_to_fit();
            code.desc.shrink_to_fit();
            if (hipaa == '1') { codes.push_back(move(code)); }
            code = ICDCode();
            code.code.reserve(codelen);
            code.dec_code.reserve(deccodelen);
            code.desc.reserve(desclen);
        }
    }

    // In case our estimate was too big, return extra memory to the system
    codes.shrink_to_fit();

    // Codes aren't necessarily in alpha order, so sort them
    // They're nearly sorted so timsort would be ideal (especially given the number of elements), but too painful to write for this small of a project
    sort(codes.begin(), codes.end(), comp_icdcode);
}

// Generate go file from codes into outp.  Read date information from year, timestamp, and dj
// For bitmask, 1 = decimal file, 2 = append ending newlines, 4 = prepend header.  Combine using bitwise or.
void gen_go_file(string *outp, vector<ICDCode> const *codes, string const *year, tm *timestamp, string *dj, char bitmask) {
    // Support function
    if (bitmask & 4) {
        char part1[15] = "", part2[3] = "", part3[15] = "";
        /******************************************************************************************************************
         * META-COMMENT:                                                                                                  *
         * This first bit is Intersystems Cache standard                                                                  *
         ******************************************************************************************************************/
        strftime(part1, 15, "%d %b %Y   ", timestamp);
        strftime(part2, 3, "%I", timestamp);
        strftime(part3, 15, ":%M %p   Cache", timestamp);
        outp->clear();
        outp->append("~Format=5.S~\n").append(part1, 14);
        outp->append(part2[0] == '0' ? part2 + sizeof(char) : part2);
        outp->append(part3, 14).append("\n^");
        /******************************************************************************************************************
         * META-COMMENT:                                                                                                  *
         * To protect potentially proprietary information, both the global name and the subscripts have been modified     *
         ******************************************************************************************************************/
        if (!(bitmask & 1)) outp->append("NON");
        outp->append("DECGBL");
        outp->append("(\"Subscript 1\")\n").append(*dj).append("_PLACEHOLDER FOR YEAR ").append(*year).push_back('\n');
    }
    for (const ICDCode &it : *codes) {
        outp->push_back('^');
        if (!(bitmask & 1)) outp->append("NON");
        outp->append("DECGBL(\"Subscript 1\",\"").append((bitmask & 1) ? it.dec_code : it.code).append("\")\n");
        outp->append(it.desc).push_back('\n');
    }
    if (bitmask & 2) outp->append("\n\n");
}

void gen_files(const vector<ICDCode> &codes, const string &year, string &dec, string &ndec, string &comb) {
    // Support function

    // Estimate roughly IND_CHARS_PER_LINE characters per code; reserve the ram to minimize reallocations
    ndec.reserve(codes.size() * IND_CHARS_PER_LINE);
    dec.reserve(codes.size() * IND_CHARS_PER_LINE);
    // Estimate COMB_CHARS_PER_LINE per code for combined files (just over twice the size of the individual files)
    comb.reserve(codes.size() * COMB_CHARS_PER_LINE);
    // Use the same estimate for the second half of the combined file as for the decimal file
    string *comb2 = new string;
    comb2->reserve(codes.size() * IND_CHARS_PER_LINE);

    using namespace chrono;
    const time_point<system_clock> now = system_clock::now();
    const time_t time = system_clock::to_time_t(now);
    tm *ltim = new tm();
    localtime_s(ltim, &time);
    /******************************************************************************************************************
     * META-COMMENT:                                                                                                  *
     * To protect potentially proprietary information, the 0 date for internal julian date indexing has been modified *
     ******************************************************************************************************************/
    string dj = move(to_string(duration_cast<days>(now.time_since_epoch()).count() - 7182));

    /*Threads
    * Yes, using threads can be dangerous.  But only ndec, dec, comb, and comb2 are being written to, and each of those
    * in a separate thread.  All data used by multiple threads is only being read.
    */
    // 6 = non-decimal, with header and footer
    thread t1(gen_go_file, &ndec, &codes, &year, ltim, &dj, 6);
    // 7 = decimal, with header and footer
    thread t2(gen_go_file, &dec, &codes, &year, ltim, &dj, 7);
    // 4 = non-decimal, with header but no footer
    thread t3(gen_go_file, &comb, &codes, &year, ltim, &dj, 4);
    // 3 = decimal, with footer but no header
    thread t4(gen_go_file, comb2, &codes, &year, ltim, &dj, 3);
    t1.join();
    t2.join();
    t3.join();
    t4.join();
    comb.append(move(*comb2));
    
    // Now that they're built, return the extra ram in case the estimates were too big
    ndec.shrink_to_fit();
    dec.shrink_to_fit();
    comb.shrink_to_fit();
}

/****************************************************************************************************************
* Main functions
****************************************************************************************************************/
// Get the latest ICD-10 page link from the cms.gov website
bool get_newest_icd10_link(ProgramState &state) {
    // Main function

    string cms_url {state.cms_base};
    cms_url.append(move(state.cms_url));

    curl_easy_setopt(state.easyhandle, CURLOPT_WRITEDATA, &state.working_data);
    curl_easy_setopt(state.easyhandle, CURLOPT_URL, cms_url.c_str());
    if (state.disp) cout << "Fetching CMS website..." << endl;
    const CURLcode res = curl_easy_perform(state.easyhandle);
    if (res != CURLE_OK) {
        cerr << "Easy perform failed to get CMS website: " << curl_easy_strerror(res) << endl;
        state.outp = OutputCode::cms_get_failed;
        return false;
    }

    if (state.disp) cout << "Locating latest ICD-10 CM link..." << endl;

    size_t menu_start, menu_end, li_start, li_end, item_start, item_end, text_start, text_end, href_start, href_end;
    string href, item_text;
    to_lower(state.working_data);

    // The ICD-10 CM link will be in the first menu class unordered list
    menu_start = state.working_data.find("<ul class=\"menu\">");
    if (menu_start != string::npos) {
        menu_end = state.working_data.find("</ul>", menu_start);
        if (menu_end != string::npos && menu_end > menu_start) {
            li_start = state.working_data.find("<li", menu_start);
            if (li_start != string::npos && li_start <= menu_end) {
                while (li_start != string::npos) {
                    li_end = state.working_data.find("</li>", li_start);
                    if (li_end == string::npos) break;

                    item_start = state.working_data.find("<a href=\"", li_start);
                    if (item_start == string::npos || item_start > li_end) break;

                    item_end = state.working_data.find("</a>", item_start);
                    if (item_end == string::npos || item_end > li_end) break;

                    href_start = state.working_data.find('"', item_start);
                    // Move href_start forward one to avoid capturing the first " character
                    if (href_start == string::npos || ++href_start > item_end) break;

                    href_end = state.working_data.find('"', href_start);
                    if (href_end == string::npos || href_end > item_end) break;

                    href = state.working_data.substr(href_start, href_end - href_start);

                    text_start = state.working_data.find('>', item_start);
                    // Move text_start forward one to avoid capturing the > character from the end of the <a> tag
                    if (text_start == string::npos || ++text_start > item_end) break;

                    text_end = state.working_data.find('<', text_start);
                    if (text_end == string::npos || text_end > item_end) break;

                    item_text = state.working_data.substr(text_start, text_end - text_start);
                    if (item_text.find("icd-10") != string::npos && item_text.find("cm") != string::npos) {
                        state.icd10_url = move(href);
                        if (state.year.empty()) state.year = move(item_text.substr(0, 4));
                        break;
                    }

                    li_start = state.working_data.find("<li", li_end);
                    if (li_start == string::npos || li_start > menu_end) break;
                }
            }
        }
    }


    if (state.icd10_url.empty()) {
        cerr << "Could not parse latest ICD-10 url from CMS webpage!" << endl;
        state.outp = OutputCode::icd10_find_failed;
        return false;
    }
    string base_url {state.icd10_url}, icd10_url_copy {};
    // If the found URL can't be parsed, prepend with the cms.gov base URL
    if (!parse_url(base_url, icd10_url_copy)) state.icd10_url = state.cms_base + state.icd10_url;
    state.working_data.clear();

    if (state.disp) cout << "Found link for " << state.year << " ICD-10 codes: " << state.icd10_url << endl;
    return true;
}

// Get the link to the tabular order zip file from the latest ICD-10 CM page
bool get_tab_order_zip_link(ProgramState &state) {
    // Main function
    if (state.icd10_url.empty()) {
        if (!get_newest_icd10_link(state)) return false;
    }
    curl_easy_setopt(state.easyhandle, CURLOPT_WRITEDATA, &state.working_data);
    curl_easy_setopt(state.easyhandle, CURLOPT_URL, state.icd10_url.c_str());

    if (state.disp) cout << "Fetching latest ICD-10 CM page..." << endl;
    const CURLcode res = curl_easy_perform(state.easyhandle);
    if (res != CURLE_OK) {
        cerr << "Easy perform failed to get latest ICD-10 page: " << curl_easy_strerror(res) << endl;
        state.outp = OutputCode::icd10_get_failed;
        return false;
    }

    if (state.disp) cout << "Locating link for tabular order codes..." << endl;

    size_t text_loc, tag_start, tag_end;
    to_lower(state.working_data);
    text_loc = state.working_data.find("tabular order");
    if (text_loc != string::npos) {
        tag_end = state.working_data.rfind("\"", text_loc);
        if (tag_end != string::npos && tag_end <= text_loc) {
            // Move tag_end back by one to avoid finding the same " character
            tag_start = state.working_data.rfind("\"", --tag_end);
            // Move tag_start forward by one to avoid capturing the first " character
            // End " is already dropped; move tag_end forward again to capture the last character from the link
            if (tag_start != string::npos && ++tag_start <= tag_end++) {
                state.zip_url = state.working_data.substr(tag_start, tag_end - tag_start);
            }
        }
    }

    if (state.zip_url.empty()) {
        cerr << "Could not locate link for tabular order zip file!" << endl;
        state.outp = OutputCode::zip_get_failed;
        return false;
    }

    string base_url {state.zip_url}, zip_url_copy {};
    // If the found URL can't be parsed, prepend with the cms.gov base URL
    if (!parse_url(base_url, zip_url_copy)) state.zip_url = state.cms_base + state.zip_url;
    state.working_data.clear();
    return true;
}

// Download the zip file from the tabular order zip file link
bool get_zip_file(ProgramState &state) {
    // Main function
    if (state.zip_url.empty()) {
        if (!get_tab_order_zip_link(state)) return false;
    }

    // Estimate ZIP_FILE_SIZE for the zip file
    state.zip_file.reserve(ZIP_FILE_SIZE);

    curl_easy_setopt(state.easyhandle, CURLOPT_WRITEDATA, &(state.zip_file));
    curl_easy_setopt(state.easyhandle, CURLOPT_URL, state.zip_url.c_str());

    if (state.disp) cout << "Fetching tabular order zip file..." << endl;
    const CURLcode res = curl_easy_perform(state.easyhandle);
    if (res != CURLE_OK) {
        cerr << "Easy perform failed to retrieve zip file: " << curl_easy_strerror(res) << endl;
        state.outp = OutputCode::zip_find_failed;
        return false;
    }

    // Return the extra space if the estimate was too big
    state.zip_file.shrink_to_fit();

    string zip_fname = state.zip_url.substr(state.zip_url.rfind("/") + 1);
    if (state.year.empty()) state.year = zip_fname.substr(0, 4);

    zip_fname = state.dest_path + zip_fname;
    if (state.disp) cout << "Saving zip file..." << endl;
    ofstream zip_file(zip_fname, ios::binary | ios::out | ios::trunc);
    zip_file << state.zip_file;
    zip_file.close();
    return true;
}

// Extract the order codes file from the tabular order zip file
bool get_codes_file(ProgramState &state) {
    // Main function
    if (state.zip_file.empty()) {
        if (!get_zip_file(state)) return false;
    }

    // Estimate ORDER_FILE_SIZE for the extracted order codes file
    state.order_file.reserve(ORDER_FILE_SIZE);

    string order_fname = ORDER_BASE + state.year + ".txt";
    if (state.disp) cout << "Extracting " << order_fname << " from zip file..." << endl;
    if (!uncompress_data(state.zip_file, order_fname, state.order_file)) {
        cerr << "Unable to extract order codes file from zip!" << endl;
        state.outp = OutputCode::extract_file_failed;
        return false;
    }

    // Return the extra ram if the estimate was too big
    state.order_file.shrink_to_fit();

    return true;
}

// Generate .go files for decimal, non-decimal, and combined codes
bool generate_go_files(ProgramState & state) {
    // Main function
    if (state.order_file.empty()) {
        if (!get_codes_file(state)) return false;
    }
    if (state.disp) cout << "Parsing ICD-10 codes and descriptions..." << endl;
    vector<ICDCode> codes;
    parse_codes(state.order_file, codes);

    if (state.disp) cout << "Generating global output files..." << endl;
    gen_files(codes, state.year, state.dec_codes, state.ndec_codes, state.comb_codes);
    return true;
}

// Main work function, compress the generated .go files to disk
bool work(ProgramState &state) {
    // Main function
    if (state.dec_codes.empty() || state.ndec_codes.empty() || state.comb_codes.empty()) {
        if (!generate_go_files(state)) return false;
    }

    if (state.disp) cout << "Compressing files..." << endl;
    // compress_data2 is an attempt to speed up the runtime (probably slightly) by passing a pre-allocated buffer instead of letting libzippp take care of it
    // It's not working at this time.
    /*
    char *test = new char[4096];
    for (int i = 0; i < 4096; i++) test[i] = 0;
    //thread t4(compress_data2, ref(state.comb_codes), "Combined version - Sunquest_ICD10_10_"+state.year, ".go", test, 1024)
    compress_data2(ref(state.comb_codes), "Combined version - Sunquest_ICD10_10_" + state.year, ".go", test, 4096);
    */

    /******************************************************************************************************************
     * META-COMMENT:                                                                                                  *
     * To protect potentially proprietary information, filenames have been modified such that they don't match        *
     * business-specific filenames                                                                                    *
     ******************************************************************************************************************/

    /*Threads
    * Again, threads can be dangerous, but again each file being written to is being touched by it's own thread
    */
    thread t1(compress_data, ref(state.ndec_codes), ref(state.dest_path), "Non-decimal version - Filename_Base_" + state.year, ".go");
    thread t2(compress_data, ref(state.dec_codes), ref(state.dest_path), "Decimal version - Filename_Base_" + state.year, ".go");
    thread t3(compress_data, ref(state.comb_codes), ref(state.dest_path), "Combined version - Filename_Base_" + state.year, ".go");
    t1.join();
    t2.join();
    t3.join();

    return true;
}

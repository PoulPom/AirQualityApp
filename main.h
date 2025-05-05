#ifndef MAIN_H
#define MAIN_H

#include <wx/wx.h>
#include <wx/thread.h>
#include <wx/listbox.h>
#include <wx/textctrl.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <string>

// Struktury
struct Sensor {
    int id;
    wxString paramName;
};

struct Station {
    int id;
    wxString name;
    wxString province;
    std::vector<Sensor> sensors;
};

// Definicje przestrzeni nazw
using json = nlohmann::json;
namespace net = boost::asio;
using tcp = net::ip::tcp;
namespace beast = boost::beast;
namespace http = beast::http;

// Użycie przestrzeni nazw std dla std::vector, std::string itp.
using std::vector;
using std::string;

// Deklaracje funkcji
bool SaveToFile(const string& data, const string& filename);
string ReadFromFile(const string& filename);
wxString fetch_data(string target, string filename, bool saveToFile = false);

// Specjalny event do aktualizacji GUI z wątku
wxDECLARE_EVENT(MY_THREAD_UPDATE_EVENT, wxThreadEvent);

#endif // MAIN_H
#include "main.h"
#include "ChartFrame.h"

// Specjalny event do aktualizacji GUI z wątku
wxDECLARE_EVENT(MY_THREAD_UPDATE_EVENT, wxThreadEvent);



// Funkcja zapisująca dane do pliku
bool SaveToFile(const std::string& data, const std::string& filename) {
    try {
        // Tworzenie katalogu, jeśli nie istnieje
        std::filesystem::path filePath(filename);
        std::filesystem::path dirPath = filePath.parent_path();
        if (!dirPath.empty() && !std::filesystem::exists(dirPath)) {
            std::filesystem::create_directories(dirPath);
        }

        // Otwieranie pliku do zapisu
        std::ofstream file(filename, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            wxLogError("Błąd: Nie można otworzyć pliku %s do zapisu.", filename.c_str());
            return false;
        }

        file << data;
        file.close();
        return true;
    }
    catch (const std::exception& e) {
        wxLogError("Błąd podczas zapisu do pliku %s: %s", filename.c_str(), e.what());
        return false;
    }
}

// Funkcja odczytująca dane z pliku
std::string ReadFromFile(const std::string& filename) {
    try {
        // Sprawdź, czy plik istnieje
        if (!std::filesystem::exists(filename)) {
            wxLogError("Plik %s nie istnieje.", filename.c_str());
            return "";
        }

        // Otwórz plik do odczytu
        std::ifstream file(filename, std::ios::in);
        if (!file.is_open()) {
            wxLogError("Nie można otworzyć pliku %s do odczytu.", filename.c_str());
            return "";
        }

        // Odczytaj zawartość pliku
        std::stringstream buffer;
        buffer << file.rdbuf();
        file.close();
        std::string content = buffer.str();

        return content;
    }
    catch (const std::exception& e) {
        wxLogError("Błąd podczas odczytu pliku %s: %s", filename.c_str(), e.what());
        return "";
    }
}

// Funkcja do pobierania danych z API
wxString fetch_data(std::string target, std::string filename, bool saveToFile) {
    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve("api.gios.gov.pl", "80");

        stream.expires_after(std::chrono::seconds(30));
        stream.connect(results);

        http::request<http::string_body> req{ http::verb::get, target, 11 };
        req.set(http::field::host, "api.gios.gov.pl");
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::dynamic_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        if (ec && ec != beast::errc::not_connected) {
            throw beast::system_error{ ec };
        }
        if (res.result() != http::status::ok) {
            std::string errorBody = beast::buffers_to_string(res.body().data());
            throw std::runtime_error("HTTP " + std::to_string(res.result_int()));
        }
        if (res.body().size() == 0) {
            throw std::runtime_error("Pusta odpowiedź");
        }

        std::string data = beast::buffers_to_string(res.body().data());

        if (saveToFile) {
            if (!SaveToFile(data, filename)) {
                return wxString::FromUTF8("Nie udało się zapisać danych do pliku " + filename);
            }
        }

        return wxString::FromUTF8(data.c_str());
    }
    catch (const std::exception& e) {
        return wxString::FromUTF8("ERROR:" + std::string(e.what()));
    }
}

// Okno główne
class MainFrame : public wxFrame {
public:
    MainFrame() : wxFrame(nullptr, wxID_ANY, "Air Quality App", wxDefaultPosition, wxSize(600, 400)) {
        wxPanel* panel = new wxPanel(this);
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        // Pole do filtrowania listy
        filtr = new wxTextCtrl(panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        filtr->SetHint("Wpisz nazwę stacji do filtrowania...");
        sizer->Add(filtr, 0, wxALL | wxEXPAND, 10);

        // Lista stacji
        stationList = new wxListBox(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        sizer->Add(stationList, 1, wxALL | wxEXPAND, 10);

        wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
        // Przyciski
        wxButton* fetchButton = new wxButton(panel, wxID_ANY, "Pobierz dane stacji");
        wxButton* historicalButton = new wxButton(panel, wxID_ANY, "Pobierz dane historyczne");
        wxButton* chartButton = new wxButton(panel, wxID_ANY, "Wyświetl dane");
        buttonSizer->Add(fetchButton, 0, wxALL, 5);
        buttonSizer->Add(historicalButton, 0, wxALL, 5);
        buttonSizer->Add(chartButton, 0, wxALL, 5);
        sizer->Add(buttonSizer, 0, wxCENTER, 10);
        // Pole tekstowe
        textCtrl = new wxTextCtrl(panel, wxID_ANY, "Ładowanie danych...", wxDefaultPosition, wxDefaultSize,
            wxTE_MULTILINE | wxTE_READONLY);
        sizer->Add(textCtrl, 1, wxALL | wxEXPAND, 10);

        sizer->SetMinSize(wxSize(600, 400)); // Ustawienie minimalnego rozmiaru okna

        panel->SetSizer(sizer);
        panel->Layout();

        fetchButton->Bind(wxEVT_BUTTON, &MainFrame::OnFetchData, this);
        historicalButton->Bind(wxEVT_BUTTON, &MainFrame::OnFetchHistoricalData, this);
        chartButton->Bind(wxEVT_BUTTON, &MainFrame::OnShowChart, this);
        filtr->Bind(wxEVT_TEXT, &MainFrame::OnFilterText, this);
        Bind(MY_THREAD_UPDATE_EVENT, &MainFrame::OnThreadUpdate, this);
        stations.clear(); // Initialize the vector
        LoadStations();
    }

private:
    void LoadStations() {
        // Pobieranie stacji z API
        wxString data = fetch_data("/pjp-api/rest/station/findAll?size=500", "database/stations.json");
        if (data.StartsWith("Błąd")) {
            textCtrl->SetValue(data);
            return;
        }

        // Parsuj JSON i wczytaj stacje
        try {
            json j = json::parse(data.ToStdString());
            stations.clear();
            stationList->Clear();
            for (const auto& station : j) {
                Station s;
                s.id = station["id"].get<int>();
                s.name = wxString::FromUTF8(station["stationName"].get<std::string>().c_str());
                s.province = wxString::FromUTF8(station["city"]["commune"]["provinceName"].get<std::string>().c_str());
                stations.push_back(s);
                wxString displayText = s.name + " (" + s.province + ")";
                // Dodajemy stację do listy i powiązujemy jej ID jako wxClientData
                stationList->Append(displayText, new wxStringClientData(std::to_string(s.id)));
            }
            textCtrl->SetValue("Wybierz stację z listy i kliknij 'Pobierz dane stacji'.");
        }
        catch (const std::exception& e) {
            textCtrl->SetValue(wxString::FromUTF8(("Błąd parsowania JSON: " + std::string(e.what())).c_str()));
        }
    }

    void LoadSensors() {
        // Otwieranie pliku ze stacjami
        std::string stationsData = ReadFromFile("database/stations.json");
        if (stationsData.empty()) {
            textCtrl->SetValue("Błąd: Nie można wczytać pliku stations.json.");
            return;
        }
        try {
            // Parsowanie JSON'a ze stacjami
            json j = json::parse(stationsData);
            if (j.empty()) {
                wxLogError("Plik stations.json jest pusty lub zawiera niepoprawny JSON.");
                textCtrl->SetValue("Błąd: Plik stations.json jest pusty.");
                return;
            }

            stations.clear();

            for (const auto& station : j) {
                Station s;
                s.id = station["id"].get<int>();
                s.name = wxString::FromUTF8(station["stationName"].get<std::string>().c_str());
                s.province = wxString::FromUTF8(station["city"]["commune"]["provinceName"].get<std::string>().c_str());

                // Pobieranie danych czujników dla tej stacji
                std::string target = "/pjp-api/v1/rest/station/sensors/" + std::to_string(s.id) + "?size=20&page=0";
                wxString sensorsData = fetch_data(target, "", false);
                if (sensorsData.StartsWith("Błąd")) {
                    wxLogError("Błąd podczas pobierania czujników dla stacji %d: %s", s.id, sensorsData.c_str());
                    continue;
                }

                json sensorsJson = json::parse(sensorsData.ToStdString());
                if (!sensorsJson.contains("Lista stanowisk pomiarowych dla podanej stacji")) {
                    wxLogMessage("Brak klucza 'Lista stanowisk pomiarowych dla podanej stacji' w odpowiedzi API dla stacji %d.", s.id);
                    continue;
                }

                auto sensorsList = sensorsJson["Lista stanowisk pomiarowych dla podanej stacji"];
                if (sensorsList.empty()) {
                    wxLogMessage("Brak czujników dla stacji %d.", s.id);
                }
                else {
                    for (const auto& sensor : sensorsList) {
                        Sensor sensorData;
                        sensorData.id = sensor["Identyfikator stanowiska"].get<int>();
                        sensorData.paramName = wxString::FromUTF8(sensor["Wskaźnik"].get<std::string>().c_str());
                        s.sensors.push_back(sensorData);
                    }
                }

                stations.push_back(s);
            }

            // Zapisywanie danych czujników do pliku sensors.json
            json sensorsOutput;
            for (const auto& station : stations) {
                json stationJson;
                stationJson["stationId"] = station.id;
                stationJson["stationName"] = station.name.ToStdString();
                json sensorsArray = json::array();
                for (const auto& sensor : station.sensors) {
                    json sensorJson;
                    sensorJson["sensorId"] = sensor.id;
                    sensorJson["paramName"] = sensor.paramName.ToStdString();
                    sensorsArray.push_back(sensorJson);
                }
                stationJson["sensors"] = sensorsArray;
                sensorsOutput.push_back(stationJson);
            }

            std::string sensorsOutputStr = sensorsOutput.dump(4);
            wxLogMessage("Zawartość pliku sensors.json przed zapisem:\n%s", sensorsOutputStr.c_str());
            if (!SaveToFile(sensorsOutputStr, "database/sensors.json")) {
                wxLogError("Nie udało się zapisać pliku sensors.json");
            }
            else {
                wxLogMessage("Zapisano czujniki do pliku sensors.json");
            }

            // Aktualizacja listy stacji w GUI
            stationList->Clear();
            for (const auto& station : stations) {
                wxString displayText = station.name + " (" + station.province + ")";
                stationList->Append(displayText, new wxStringClientData(std::to_string(station.id)));
            }
            textCtrl->SetValue("Wybierz stację z listy i kliknij 'Pobierz dane stacji'.");
        }
        catch (const std::exception& e) {
            wxLogError("Błąd podczas wczytywania czujników: %s", e.what());
            textCtrl->SetValue(wxString::FromUTF8(("Błąd wczytywania czujników: " + std::string(e.what())).c_str()));
        }
    }

    void OnFetchData(wxCommandEvent& event) {
        int selection = stationList->GetSelection();
        if (selection == wxNOT_FOUND) {
            textCtrl->SetValue("Proszę wybrać stację z listy.");
            return;
        }

        // Pobierz ID stacji z danych klienta
        wxStringClientData* clientData = dynamic_cast<wxStringClientData*>(stationList->GetClientObject(selection));
        if (!clientData) {
            textCtrl->SetValue("Błąd: Brak danych stacji.");
            return;
        }
        int stationId = wxAtoi(clientData->GetData());

        // Znajdź stację w wektorze stations
        Station selectedStation;
        bool found = false;
        for (const auto& station : stations) {
            if (station.id == stationId) {
                selectedStation = station;
                found = true;
                break;
            }
        }
        if (!found) {
            textCtrl->SetValue("Błąd: Nie znaleziono wybranej stacji.");
            return;
        }

        std::thread([this, selectedStation]() {
            std::string target = "/pjp-api/v1/rest/station/sensors/" + std::to_string(selectedStation.id) + "?size=20&page=0";
            wxString sensorsData = fetch_data(target, "", false);
            if (sensorsData.StartsWith("ERROR:")) {
                wxThreadEvent* event = new wxThreadEvent(MY_THREAD_UPDATE_EVENT);
                event->SetString("Błąd podczas pobierania czujników: " + sensorsData.AfterFirst(':'));
                wxQueueEvent(this, event);
                return;
            }

            std::vector<Sensor> sensors;
            try {
                json sensorsJson = json::parse(sensorsData.ToStdString());
                if (!sensorsJson.contains("Lista stanowisk pomiarowych dla podanej stacji")) {
                    wxThreadEvent* event = new wxThreadEvent(MY_THREAD_UPDATE_EVENT);
                    event->SetString("Błąd: Nieprawidłowy format odpowiedzi API dla czujników.");
                    wxQueueEvent(this, event);
                    return;
                }

                auto sensorsList = sensorsJson["Lista stanowisk pomiarowych dla podanej stacji"];
                if (sensorsList.empty()) {
                    wxThreadEvent* event = new wxThreadEvent(MY_THREAD_UPDATE_EVENT);
                    event->SetString("Brak czujników dla tej stacji.");
                    wxQueueEvent(this, event);
                    return;
                }

                for (const auto& sensor : sensorsList) {
                    Sensor sensorData;
                    sensorData.id = sensor["Identyfikator stanowiska"].get<int>();
                    sensorData.paramName = wxString::FromUTF8(sensor["Wskaźnik"].get<std::string>().c_str());
                    sensors.push_back(sensorData);
                }
            }
            catch (const std::exception& e) {
                wxThreadEvent* event = new wxThreadEvent(MY_THREAD_UPDATE_EVENT);
                event->SetString(wxString::FromUTF8(("Błąd parsowania danych czujników: " + std::string(e.what())).c_str()));
                wxQueueEvent(this, event);
                return;
            }

            json sensorsOutput;
            json stationJson;
            stationJson["stationId"] = selectedStation.id;
            stationJson["stationName"] = selectedStation.name.ToStdString();
            json sensorsArray = json::array();
            for (const auto& sensor : sensors) {
                json sensorJson;
                sensorJson["sensorId"] = sensor.id;
                sensorJson["paramName"] = sensor.paramName.ToStdString();
                sensorsArray.push_back(sensorJson);
            }
            stationJson["sensors"] = sensorsArray;
            sensorsOutput.push_back(stationJson);

            std::string sensorsOutputStr = sensorsOutput.dump(4);
            if (!SaveToFile(sensorsOutputStr, "database/sensors.json")) {
                wxThreadEvent* event = new wxThreadEvent(MY_THREAD_UPDATE_EVENT);
                event->SetString("Błąd: Nie udało się zapisać pliku sensors.json.");
                wxQueueEvent(this, event);
                return;
            }

            wxString formattedData = "Dane dla stacji " + selectedStation.name + ":\n";
            for (const Sensor& sensor : sensors) {
                formattedData += wxString::Format("\nCzujnik: %s (ID: %d)\n", sensor.paramName, sensor.id);
                std::string dataTarget = "/pjp-api/v1/rest/data/getData/" + std::to_string(sensor.id) + "?size=500&page=0";
                wxString data = fetch_data(dataTarget, "database/data.json", true);
                if (data.StartsWith("ERROR:")) {
                    wxString errorMsg = data.AfterFirst(':');
                    if (errorMsg.Contains("HTTP 400")) {
                        formattedData += wxString::Format("Brak danych w API (HTTP 400)\n");
                    }
                    else {
                        formattedData += wxString::Format("Błąd: %s\n", errorMsg);
                    }
                    continue;
                }

                try {
                    // Sprawdź, czy odpowiedź jest poprawnym JSON-em
                    if (data.StartsWith("Error") || data.empty()) {
                        formattedData += wxString::Format("Błąd: Nieprawidłowa odpowiedź API: %s\n", data);
                        continue;
                    }

                    json j = json::parse(data.ToStdString());
                    if (j.contains("Lista danych pomiarowych")) {
                        auto measurements = j["Lista danych pomiarowych"];
                        if (measurements.empty()) {
                            formattedData += "Brak danych pomiarowych.\n";
                            continue;
                        }

                        for (const auto& measurement : measurements) {
                            if (measurement.contains("Wartość") && !measurement["Wartość"].is_null()) {
                                double value = measurement["Wartość"].get<double>();
                                wxString date = wxString::FromUTF8(measurement["Data"].get<std::string>().c_str());
                                wxString code = wxString::FromUTF8(measurement["Kod stanowiska"].get<std::string>().c_str());
                                formattedData += wxString::Format("- %s: %.2f (data: %s)\n", code, value, date);
                            }
                        }
                    }
                    else {
                        formattedData += "Brak klucza 'Lista danych pomiarowych' w odpowiedzi API.\n";
                    }
                }
                catch (const std::exception& e) {
                    wxLogError("Błąd parsowania danych dla czujnika %d (%s): %s", sensor.id, sensor.paramName.c_str(), e.what());
                    formattedData += wxString::Format("Błąd parsowania danych: %s\n", e.what());
                }
            }

            wxThreadEvent* event = new wxThreadEvent(MY_THREAD_UPDATE_EVENT);
            event->SetString(formattedData);
            wxQueueEvent(this, event);
            }).detach();
    }

    void OnFetchHistoricalData(wxCommandEvent& event) {
        int selection = stationList->GetSelection();
        if (selection == wxNOT_FOUND) {
            textCtrl->SetValue("Proszę wybrać stację z listy.");
            return;
        }

        // Pobierz ID stacji z danych klienta
        wxStringClientData* clientData = dynamic_cast<wxStringClientData*>(stationList->GetClientObject(selection));
        if (!clientData) {
            textCtrl->SetValue("Błąd: Brak danych stacji.");
            return;
        }
        int stationId = wxAtoi(clientData->GetData());

        // Znajdź stację w wektorze stations
        Station selectedStation;
        bool found = false;
        for (const auto& station : stations) {
            if (station.id == stationId) {
                selectedStation = station;
                found = true;
                break;
            }
        }
        if (!found) {
            textCtrl->SetValue("Błąd: Nie znaleziono wybranej stacji.");
            return;
        }

        std::thread([this, selectedStation]() {
            std::string target = "/pjp-api/v1/rest/station/sensors/" + std::to_string(selectedStation.id) + "?size=20&page=0";
            wxString sensorsData = fetch_data(target, "", false);
            if (sensorsData.StartsWith("ERROR:")) {
                wxThreadEvent* event = new wxThreadEvent(MY_THREAD_UPDATE_EVENT);
                event->SetString("Błąd podczas pobierania czujników: " + sensorsData.AfterFirst(':'));
                wxQueueEvent(this, event);
                return;
            }

            std::vector<Sensor> sensors;
            try {
                json sensorsJson = json::parse(sensorsData.ToStdString());
                if (!sensorsJson.contains("Lista stanowisk pomiarowych dla podanej stacji")) {
                    wxThreadEvent* event = new wxThreadEvent(MY_THREAD_UPDATE_EVENT);
                    event->SetString("Błąd: Nieprawidłowy format odpowiedzi API dla czujników.");
                    wxQueueEvent(this, event);
                    return;
                }

                auto sensorsList = sensorsJson["Lista stanowisk pomiarowych dla podanej stacji"];
                if (sensorsList.empty()) {
                    wxThreadEvent* event = new wxThreadEvent(MY_THREAD_UPDATE_EVENT);
                    event->SetString("Brak czujników dla tej stacji.");
                    wxQueueEvent(this, event);
                    return;
                }

                for (const auto& sensor : sensorsList) {
                    Sensor sensorData;
                    sensorData.id = sensor["Identyfikator stanowiska"].get<int>();
                    sensorData.paramName = wxString::FromUTF8(sensor["Wskaźnik"].get<std::string>().c_str());
                    sensors.push_back(sensorData);
                }
            }
            catch (const std::exception& e) {
                wxThreadEvent* event = new wxThreadEvent(MY_THREAD_UPDATE_EVENT);
                event->SetString(wxString::FromUTF8(("Błąd parsowania danych czujników: " + std::string(e.what())).c_str()));
                wxQueueEvent(this, event);
                return;
            }

            wxString formattedData = "Dane historyczne dla stacji " + selectedStation.name + " (ostatnie 5 dni):\n";
            for (const Sensor& sensor : sensors) {
                formattedData += wxString::Format("\nCzujnik: %s (ID: %d)\n", sensor.paramName, sensor.id);
                std::string dataTarget = "/pjp-api/v1/rest/archivalData/getDataBySensor/" + std::to_string(sensor.id) + "?size=500&dayNumber=5";
                wxString data = fetch_data(dataTarget, "database/historical_data.json", true);
                if (data.StartsWith("ERROR:")) {
                    wxString errorMsg = data.AfterFirst(':');
                    if (errorMsg.Contains("HTTP 400")) {
                        formattedData += wxString::Format("Brak danych w API (HTTP 400)\n");
                    }
                    else {
                        formattedData += wxString::Format("Błąd: %s\n", errorMsg);
                    }
                    continue;
                }

                try {
                    // Sprawdź, czy odpowiedź jest poprawnym JSON-em
                    if (data.StartsWith("Error") || data.empty()) {
                        formattedData += wxString::Format("Błąd: Nieprawidłowa odpowiedź API: %s\n", data);
                        continue;
                    }

                    json j = json::parse(data.ToStdString());
                    if (j.contains("Lista archiwalnych wyników pomiarów")) {
                        auto measurements = j["Lista archiwalnych wyników pomiarów"];
                        if (measurements.empty()) {
                            formattedData += "Brak danych pomiarowych.\n";
                            continue;
                        }

                        for (const auto& measurement : measurements) {
                            if (measurement.contains("Wartość") && !measurement["Wartość"].is_null()) {
                                double value = measurement["Wartość"].get<double>();
                                wxString date = wxString::FromUTF8(measurement["Data"].get<std::string>().c_str());
                                wxString code = wxString::FromUTF8(measurement["Kod stanowiska"].get<std::string>().c_str());
                                formattedData += wxString::Format("- %s: %.2f (data: %s)\n", code, value, date);
                            }
                        }
                    }
                    else {
                        formattedData += "Brak klucza 'Lista archiwalnych wyników pomiarów' w odpowiedzi API.\n";
                    }
                }
                catch (const std::exception& e) {
                    wxLogError("Błąd parsowania danych historycznych dla czujnika %d (%s): %s", sensor.id, sensor.paramName.c_str(), e.what());
                    formattedData += wxString::Format("Błąd parsowania danych: %s\n", e.what());
                }
            }

            wxThreadEvent* event = new wxThreadEvent(MY_THREAD_UPDATE_EVENT);
            event->SetString(formattedData);
            wxQueueEvent(this, event);
            }).detach();
    }

    void OnThreadUpdate(wxThreadEvent& event) {
        wxString message = event.GetString();
        textCtrl->SetValue(message);
    }

    void OnFilterText(wxCommandEvent& event) {
        wxString filterText = filtr->GetValue().Lower();
        filterText.Replace(",", "");
        stationList->Clear();

        for (const auto& station : stations) {
            wxString stationName = station.name.Lower();
            stationName.Replace(",", "");
            wxString province = station.province.Lower();
            province.Replace(",", "");

            if (stationName.Contains(filterText) || province.Contains(filterText) || filterText.IsEmpty()) {
                wxString displayText = station.name + " (" + station.province + ")";
                // Dodajemy stację do listy i powiązujemy jej ID jako wxClientData
                stationList->Append(displayText, new wxStringClientData(std::to_string(station.id)));
            }
        }
    }

    void OnShowChart(wxCommandEvent& event) {
        int selection = stationList->GetSelection();

        if (selection == wxNOT_FOUND) {
            textCtrl->SetValue("Proszę wybrać stację z listy.");
            return;
        }

        // Pobierz ID stacji z danych klienta
        wxStringClientData* clientData = dynamic_cast<wxStringClientData*>(stationList->GetClientObject(selection));
        if (!clientData) {
            textCtrl->SetValue("Błąd: Brak danych stacji.");
            return;
        }
        int stationId = wxAtoi(clientData->GetData());

        // Znajdź stację w wektorze stations
        auto stationIt = std::find_if(stations.begin(), stations.end(),
            [stationId](const Station& s) { return s.id == stationId; });
        if (stationIt == stations.end()) {
            textCtrl->SetValue("Błąd: Nie znaleziono wybranej stacji.");
            return;
        }

        Station& selectedStation = *stationIt;

        // Sprawdź, czy istnieje zapisany plik z danymi
        if (!std::filesystem::exists("database/data.json")) {
            textCtrl->SetValue("Błąd: Brak zapisanych danych. Najpierw pobierz dane stacji.");
            return;
        }

        // Pobierz sensory dla wybranej stacji (najpierw sprawdź zapisane)
        std::vector<Sensor> sensors;
        bool sensorsLoaded = false;

        // Próba wczytania czujników z pliku sensors.json
        if (std::filesystem::exists("database/sensors.json")) {
            std::string sensorsContent = ReadFromFile("database/sensors.json");
            if (!sensorsContent.empty()) {
                try {
                    nlohmann::json j = nlohmann::json::parse(sensorsContent);
                    for (const auto& stationData : j) {
                        if (stationData["stationId"].get<int>() == stationId) {
                            auto sensorsArray = stationData["sensors"];
                            for (const auto& sensor : sensorsArray) {
                                Sensor s;
                                s.id = sensor["sensorId"].get<int>();
                                s.paramName = wxString::FromUTF8(sensor["paramName"].get<std::string>().c_str());
                                sensors.push_back(s);
                            }
                            sensorsLoaded = true;
                            break;
                        }
                    }
                }
                catch (const std::exception& e) {
                    wxLogError("Błąd podczas wczytywania czujników z pliku: %s", e.what());
                }
            }
        }

        // Jeśli nie udało się wczytać z pliku, pobierz z API
        if (!sensorsLoaded) {
            std::string target = "/pjp-api/v1/rest/station/sensors/" + std::to_string(selectedStation.id) + "?size=20&page=0";
            wxString sensorsData = fetch_data(target, "", false);
            if (sensorsData.StartsWith("ERROR:")) {
                textCtrl->SetValue("Błąd podczas pobierania czujników dla stacji " + selectedStation.name + ": " + sensorsData.AfterFirst(':'));
                return;
            }

            try {
                nlohmann::json sensorsJson = nlohmann::json::parse(sensorsData.ToStdString());
                if (!sensorsJson.contains("Lista stanowisk pomiarowych dla podanej stacji")) {
                    textCtrl->SetValue("Błąd: Nieprawidłowy format odpowiedzi API dla czujników stacji " + selectedStation.name + ".");
                    return;
                }

                auto sensorsList = sensorsJson["Lista stanowisk pomiarowych dla podanej stacji"];
                if (sensorsList.empty()) {
                    textCtrl->SetValue("Brak czujników dla stacji " + selectedStation.name + ".");
                    return;
                }

                for (const auto& sensor : sensorsList) {
                    Sensor sensorData;
                    if (!sensor.contains("Identyfikator stanowiska") || !sensor.contains("Wskaźnik")) {
                        wxLogError("Brak wymaganych pól w danych czujnika dla stacji %d.", selectedStation.id);
                        continue;
                    }
                    sensorData.id = sensor["Identyfikator stanowiska"].get<int>();
                    sensorData.paramName = wxString::FromUTF8(sensor["Wskaźnik"].get<std::string>().c_str());
                    sensors.push_back(sensorData);
                }

                if (sensors.empty()) {
                    textCtrl->SetValue("Nie znaleziono żadnych prawidłowych czujników dla stacji " + selectedStation.name + ".");
                    return;
                }

                // Zaktualizuj sensory w selectedStation
                selectedStation.sensors = sensors;

                // Zapisz zaktualizowane sensory do pliku sensors.json
                nlohmann::json sensorsOutput;
                for (const auto& station : stations) {
                    nlohmann::json stationJson;
                    stationJson["stationId"] = station.id;
                    stationJson["stationName"] = station.name.ToStdString();
                    nlohmann::json sensorsArray = nlohmann::json::array();
                    for (const auto& sensor : station.sensors) {
                        nlohmann::json sensorJson;
                        sensorJson["sensorId"] = sensor.id;
                        sensorJson["paramName"] = sensor.paramName.ToStdString();
                        sensorsArray.push_back(sensorJson);
                    }
                    stationJson["sensors"] = sensorsArray;
                    sensorsOutput.push_back(stationJson);
                }
                std::string sensorsOutputStr = sensorsOutput.dump(4);
                SaveToFile(sensorsOutputStr, "database/sensors.json");
            }
            catch (const std::exception& e) {
                textCtrl->SetValue("Błąd parsowania danych czujników dla stacji " + selectedStation.name + ": " + wxString::FromUTF8(e.what()));
                return;
            }
        }

        // Otwórz nowe okno z wykresem
        ChartFrame* chartFrame = new ChartFrame(selectedStation, sensors);
        chartFrame->Show(true);
    }

    wxListBox* stationList;
    wxTextCtrl* textCtrl;
    wxTextCtrl* filtr;
    std::vector<Station> stations;
};

// Aplikacja
class MyApp : public wxApp {
public:
    bool OnInit() override {
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

// Definicja zdarzeń
wxDEFINE_EVENT(MY_THREAD_UPDATE_EVENT, wxThreadEvent);

wxIMPLEMENT_APP(MyApp);
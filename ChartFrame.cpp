#include "ChartFrame.h"
#include <wx/datetime.h>
#include <random>
#include <limits>

wxBEGIN_EVENT_TABLE(LegendPanel, wxPanel)
EVT_PAINT(LegendPanel::OnPaint)
wxEND_EVENT_TABLE()

LegendPanel::LegendPanel(wxWindow* parent, const std::vector<wxString>& labels, const std::vector<wxColour>& colors)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(200, -1)), labels_(labels), colors_(colors) {
    SetBackgroundColour(*wxWHITE);
}

void LegendPanel::OnPaint(wxPaintEvent& event) {
    wxPaintDC dc(this);
    int y = 10;
    const int rectSize = 10;
    const int spacing = 5;

    for (size_t i = 0; i < labels_.size(); ++i) {
        // Rysuj prostokąt w odpowiednim kolorze
        dc.SetBrush(wxBrush(colors_[i]));
        dc.SetPen(wxPen(colors_[i]));
        dc.DrawRectangle(10, y, rectSize, rectSize);

        // Rysuj tekst obok prostokąta
        dc.SetPen(*wxBLACK_PEN);
        dc.DrawText(labels_[i], 10 + rectSize + spacing, y);
        y += rectSize + spacing;
    }
}

ChartFrame::ChartFrame(const Station& station, const std::vector<Sensor>& sensors)
    : wxFrame(nullptr, wxID_ANY, "Wykres danych dla " + station.name, wxDefaultPosition, wxSize(1000, 600)) {
    wxPanel* mainPanel = new wxPanel(this);
    wxBoxSizer* mainSizer = new wxBoxSizer(wxHORIZONTAL);

    wxPanel* plotPanel = new wxPanel(mainPanel);
    wxBoxSizer* plotSizer = new wxBoxSizer(wxVERTICAL);

    plot = new mpWindow(plotPanel, wxID_ANY);
    plotSizer->Add(plot, 1, wxEXPAND | wxALL, 10);

    // Zablokuj wszystkie interakcje myszą
    plot->EnableMousePanZoom(false);

    plotPanel->SetSizer(plotSizer);

    // Panel na legendę
    legendContainer = new wxPanel(mainPanel); // Przechowujemy wskaźnik
    wxBoxSizer* legendSizer = new wxBoxSizer(wxVERTICAL);

    wxStaticText* legendLabel = new wxStaticText(legendContainer, wxID_ANY, "Legenda:");
    legendSizer->Add(legendLabel, 0, wxALL, 5);

    // Utwórz legendę (pusta na razie, wypełnimy w FetchAndPlotData)
    std::vector<wxString> emptyLabels;
    std::vector<wxColour> emptyColors;
    legendPanel = new LegendPanel(legendContainer, emptyLabels, emptyColors);
    legendSizer->Add(legendPanel, 1, wxEXPAND | wxALL, 5);

    legendContainer->SetSizer(legendSizer);

    // Dodaj panele do głównego sizer'a
    mainSizer->Add(plotPanel, 1, wxEXPAND | wxALL, 5);
    mainSizer->Add(legendContainer, 0, wxEXPAND | wxALL, 5);

    mainPanel->SetSizer(mainSizer);
    mainPanel->Layout();

    // Pobierz i narysuj dane
    FetchAndPlotData(station, sensors);
}

void ChartFrame::FetchAndPlotData(const Station& station, const std::vector<Sensor>& sensors) {
    // Wyczyść kolory i legendę
    sensorColors.clear();
    std::vector<wxString> legendLabels;

    wxDateTime now = wxDateTime::Now();
    wxDateTime threeDaysAgo = now - wxTimeSpan::Days(3);

    mpScaleX* xaxis = new mpScaleX("Czas (godziny wstecz)", mpALIGN_BOTTOM, true);
    mpScaleY* yaxis = new mpScaleY("Wartość", mpALIGN_LEFT, true);
    xaxis->SetTicks(true)
    yaxis->SetTicks(true);
    xaxis->SetLabelFormat("%.0f");

    // Generator kolorów dla różnych sensorów
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    // Zmienne do obliczania zakresu osi Y
    double yMin = std::numeric_limits<double>::max();
    double yMax = std::numeric_limits<double>::lowest();

    for (const auto& sensor : sensors) {
        // Pobierz dane dla sensora
        std::string target = "/pjp-api/v1/rest/data/getData/" + std::to_string(sensor.id) + "?size=500&page=0";
        wxString data = fetch_data(target, "", false);
        if (data.StartsWith("ERROR:")) {
            wxLogError("Błąd pobierania danych dla sensora %d: %s", sensor.id, data.c_str());
            continue;
        }

        try {
            nlohmann::json j = nlohmann::json::parse(data.ToStdString());
            if (!j.contains("Lista danych pomiarowych")) {
                wxLogError("Brak danych pomiarowych dla sensora %d", sensor.id);
                continue;
            }

            std::vector<double> x_values;
            std::vector<double> y_values;

            for (const auto& measurement : j["Lista danych pomiarowych"]) {
                if (measurement.contains("Wartość") && !measurement["Wartość"].is_null()) {
                    wxString dateStr = wxString::FromUTF8(measurement["Data"].get<std::string>().c_str());
                    wxDateTime date;
                    date.ParseFormat(dateStr, "%Y-%m-%d %H:%M:%S");

                    if (!date.IsValid() || date < threeDaysAgo || date > now) {
                        continue;
                    }

                    // Oblicz czas w godzinach wstecz od teraz
                    wxTimeSpan diff = now - date;
                    double hours = diff.GetHours() + diff.GetMinutes() / 60.0;
                    x_values.push_back(hours);

                    double value = measurement["Wartość"].get<double>();
                    y_values.push_back(value);

                    // Aktualizuj yMin i yMax
                    yMin = std::min(yMin, value);
                    yMax = std::max(yMax, value);
                }
            }

            if (!x_values.empty()) {
                // Generuj losowy kolor dla sensora
                wxColour color(dis(gen), dis(gen), dis(gen));

                // warstwę dla sensora
                mpFXYVector* layer = new mpFXYVector("");
                layer->SetData(x_values, y_values);
                layer->SetContinuity(true);
                wxPen pen(color, 2); 
                layer->SetPen(pen);
                layer->SetDrawOutsideMargins(false);
                plot->AddLayer(layer);

                // Dodaje sensory do legendy
                legendLabels.push_back(sensor.paramName);
                sensorColors.push_back(color);
            }
        }
        catch (const std::exception& e) {
            wxLogError("Błąd parsowania danych dla sensora %d: %s", sensor.id, e.what());
        }
    }

    // Aktualizuje legendę
    legendPanel->Destroy();
    legendPanel = new LegendPanel(legendContainer, legendLabels, sensorColors);
    legendContainer->GetSizer()->Add(legendPanel, 1, wxEXPAND | wxALL, 5);
    legendContainer->Layout();

    // Dodaje osie
    plot->AddLayer(xaxis);
    plot->AddLayer(yaxis);

    // Zablokowanie przewijania i zoomowania myszą
    plot->EnableMousePanZoom(false);

    double maxHours = 3 * 24.0; // 3 dni to 72 godziny

    // Jeśli nie ma danych, ustaw domyślny zakres osi Y
    if (yMin > yMax) { // Brak danych
        yMin = 0.0;
        yMax = 72.0;
    }
    else {
        yMin = std::min(yMin, 0.0);
        yMax = std::max(yMax, 0.0);
        // margines do zakresu Y (np. 10% z każdej strony)
        double margin = (yMax - yMin) * 0.1;
        yMin -= margin;
        yMax += margin;
    }

    plot->Fit(0.0, maxHours, yMin, yMax);
    plot->UpdateAll();
}
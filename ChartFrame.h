#ifndef CHART_FRAME_H
#define CHART_FRAME_H

#include <wx/wx.h>
#include "mathplot.h"
#include <nlohmann/json.hpp>
#include <vector>
#include "main.h"

class LegendPanel : public wxPanel {
public:
    LegendPanel(wxWindow* parent, const std::vector<wxString>& labels, const std::vector<wxColour>& colors);
    void OnPaint(wxPaintEvent& event);

private:
    std::vector<wxString> labels_;
    std::vector<wxColour> colors_;
    wxDECLARE_EVENT_TABLE();
};

class ChartFrame : public wxFrame {
public:
    ChartFrame(const Station& station, const std::vector<Sensor>& sensors);

private:
    void FetchAndPlotData(const Station& station, const std::vector<Sensor>& sensors);
    mpWindow* plot;
    LegendPanel* legendPanel;
    wxPanel* legendContainer;
    std::vector<wxColour> sensorColors;
};

#endif // CHART_FRAME_H
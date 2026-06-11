/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

#include "imgui.h"
#include "imgui_internal.h" /* DockBuilderDockWindow, DockBuilderFinish (internal API) */
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <cfloat>
#include <stdio.h>
#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

#include "implot.h"
#include "openair1/PHY/defs_nr_UE.h"
extern "C" {
#include "openair1/PHY/TOOLS/phy_scope_interface.h"
#include "executables/softmodem-common.h"
}
#include <iostream>
#include <vector>
#include <limits>
#include <algorithm>
#include <sstream>
#include <string>
#include <mutex>
#include <thread>
#include <fstream>
#include <cstring>
#include "imscope_internal.h"
#include <cstdlib>
#include <vector>

#define NR_MAX_RB 273
#define N_SC_PER_RB NR_NB_SC_PER_RB

static std::vector<int> rb_boundaries;

void copyDataThreadSafe(void *scopeData,
                        enum scopeDataType type,
                        void *dataIn,
                        int elementSz,
                        int colSz,
                        int lineSz,
                        int offset,
                        metadata *meta);
bool tryLockScopeData(enum scopeDataType type, int elementSz, int colSz, int lineSz, metadata *meta);
void copyDataUnsafeWithOffset(enum scopeDataType type, void *dataIn, size_t size, size_t offset, int copy_index);
void unlockScopeData(enum scopeDataType type);

static void glfw_error_callback(int error, const char *description)
{
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

MovingAverageTimer iq_procedure_timer;

ImScopeDataWrapper scope_array[EXTRA_SCOPE_TYPES];

class LLRPlot {
  int len = 0;
  float timestamp = 0;
  std::vector<int16_t> llr;
  bool frozen = false;
  bool next = false;
  metadata meta;

 public:
  void Draw(float time, enum scopeDataType type, const char *label)
  {
    ImGui::BeginGroup();
    if (ImGui::Button(frozen ? "Unfreeze" : "Freeze")) {
      frozen = !frozen;
      next = false;
    }
    if (frozen) {
      ImGui::SameLine();
      ImGui::BeginDisabled(next);
      if (ImGui::Button("Load next histogram")) {
        next = true;
      }
      ImGui::EndDisabled();
    }

    ImScopeDataWrapper &scope_data = scope_array[type];
    const ImVec2 llr_avail = ImGui::GetContentRegionAvail();
    if (llr_avail.x > 2.f && llr_avail.y > 2.f && ImPlot::BeginPlot(label)) {
      ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
      if (!frozen || next) {
        if (scope_data.is_data_ready) {
          iq_procedure_timer.Add(scope_data.data.time_taken_in_ns);
          timestamp = time;
          const int16_t *tmp = (int16_t *)(scope_data.data.scope_graph_data + 1);
          len = scope_data.data.scope_graph_data->lineSz;
          llr.reserve(len);
          for (auto i = 0; i < len; i++) {
            llr[i] = tmp[i];
          }
          meta = scope_data.data.meta;
          scope_data.is_data_ready = false;
          if (frozen) {
            next = false;
          }
        }
      }

      ImPlot::PlotLine(label, llr.data(), len);
      ImPlot::EndPlot();
    }
    std::stringstream ss;
    if (meta.slot != -1) {
      ss << " slot: " << meta.slot;
    }
    if (meta.frame != -1) {
      ss << " frame: " << meta.frame;
    }
    if (!ss.str().empty()) {
      ImGui::Text("Data for %s", ss.str().c_str());
    }
    ImGui::Text("Data is %.2f seconds old", time - timestamp);
    ImGui::EndGroup();
  }
};

class IQHist {
 private:
  bool frozen = false;
  bool next = false;
  float range = 100;
  int num_bins = 33;
  std::string label;
  float min_nonzero_percentage = 0.9;
  float epsilon = 0.0;
  bool auto_adjust_range = true;
  int plot_type = 0;
  bool disable_scatterplot;

 public:
  IQHist(const char *label_, bool _disable_scatterplot = false)
  {
    label = label_;
    disable_scatterplot = _disable_scatterplot;
  };
  bool ShouldReadData(void)
  {
    return !frozen || next;
  }
  float GetEpsilon(void)
  {
    return epsilon;
  }
  void Draw(IQData *iq_data, float time, bool new_data)
  {
    if (new_data && frozen && next) {
      // Evaluate if new data matches filter settings
      if (((float)iq_data->nonzero_count / (float)iq_data->len) > min_nonzero_percentage) {
        next = false;
      }
    }
    ImGui::BeginGroup();
    ImGui::Checkbox("auto adjust range", &auto_adjust_range);
    if (auto_adjust_range) {
      if (range < iq_data->max_iq * 1.1) {
        range = iq_data->max_iq * 1.1;
      }
    }
    ImGui::BeginDisabled(auto_adjust_range);
    ImGui::SameLine();
    ImGui::DragFloat("Range", &range, 1, 0.1, std::numeric_limits<int16_t>::max());
    ImGui::EndDisabled();

    ImGui::DragInt("Number of bins", &num_bins, 1, 33, 101);
    if (ImGui::Button(frozen ? "Unfreeze" : "Freeze")) {
      frozen = !frozen;
      next = false;
    }

    if (frozen) {
      ImGui::SameLine();
      ImGui::BeginDisabled(next);
      if (ImGui::Button("Load next histogram")) {
        next = true;
      }
      ImGui::EndDisabled();
      ImGui::Text("Filter parameters");
      ImGui::DragFloat("%% nonzero elements", &min_nonzero_percentage, 1, 0.0, 100);
      ImGui::DragFloat("epsilon", &epsilon, 1, 0.0, 3000);
    }
    const char *items[] = {"Histogram", "RMS", "Scatter"};
    ImGui::Combo("Select plot type", &plot_type, items, disable_scatterplot ? 2 : 3);
    /* Skip plot when content region is too small (e.g. window being moved/docked) to avoid ImPlot/ImGui stack issues. */
    const ImVec2 plot_avail = ImGui::GetContentRegionAvail();
    if (plot_avail.x > 2.f && plot_avail.y > 2.f) {
      if (plot_type == 0) {
        float x = ImGui::CalcItemWidth();
        if (x > 2.f && ImPlot::BeginPlot(label.c_str(), {x, x})) {
          ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
          ImPlot::PlotHistogram2D(label.c_str(),
                                  iq_data->real.data(),
                                  iq_data->imag.data(),
                                  iq_data->len,
                                  num_bins,
                                  num_bins,
                                  ImPlotRect(-range, range, -range, range));
          ImPlot::EndPlot();
        }
      } else if (plot_type == 2) {
        float x = ImGui::CalcItemWidth();
        if (x > 2.f && ImPlot::BeginPlot(label.c_str(), {x, x})) {
          ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
          int points_drawn = 0;
          while (points_drawn < iq_data->len) {
            // Limit the amount of data plotted with PlotScatter call (issue with vertices/draw call)
            int points_to_draw = std::min(iq_data->len - points_drawn, 16000);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 1, IMPLOT_AUTO_COL, 1);
            ImPlot::PlotScatter(label.c_str(),
                                iq_data->real.data() + points_drawn,
                                iq_data->imag.data() + points_drawn,
                                points_to_draw);
            points_drawn += points_to_draw;
          }
          ImPlot::EndPlot();
        }
      } else if (plot_type == 1) {
        if (ImPlot::BeginPlot(label.c_str())) {
          ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
          ImPlot::PlotLine(label.c_str(), iq_data->power.data(), iq_data->len);
          ImPlot::EndPlot();
        }
      }
    }
    ImGui::Text("Maximum value = %d, nonzero elements/total %d/%d", iq_data->max_iq, iq_data->nonzero_count, iq_data->len);
    ImGui::Text("Data is %.2f seconds old", time - iq_data->timestamp);
    std::stringstream ss;
    if (iq_data->meta.slot != -1) {
      ss << " slot: " << iq_data->meta.slot;
    }
    if (iq_data->meta.frame != -1) {
      ss << " frame: " << iq_data->meta.frame;
    }
    if (!ss.str().empty()) {
      ImGui::Text("Data for %s", ss.str().c_str());
    }
    if (ImGui::Button("Save IQ")) {
      std::stringstream ss;
      ss << "iq_";
      if (iq_data->meta.frame != -1) {
        ss << iq_data->meta.frame << "_";
      }
      if (iq_data->meta.slot != -1) {
        ss << iq_data->meta.slot << "_";
      }
      ss << scope_id_to_string(static_cast<enum scopeDataType>(iq_data->scope_id));
      ss << ".csv";
      std::ofstream file(ss.str());
      if (file.is_open()) {
        file << "real;imag\n";
        for (int i = 0; i < iq_data->len; i++) {
          file << iq_data->real[i] << ";" << iq_data->imag[i] << "\n";
        }
        file.close();
        std::cout << "Saved IQ to file: " << ss.str() << std::endl;
      } else {
        std::cerr << "Unable to open file";
      }
    }
    ImGui::EndGroup();
  }
};

class IQSlotHeatmap {
 private:
  bool frozen = false;
  bool next = false;
  float timestamp = 0;
  std::vector<float> power;
  ImScopeDataWrapper *scope_data;
  std::string label;
  int len = 0;
  float max = 0;
  float stop_at_min = 1000;

 public:
  IQSlotHeatmap(ImScopeDataWrapper *scope_data_, const char *label_)
  {
    scope_data = scope_data_;
    label = label_;
  };
  // Read in the data from the sink and transform it for the use by the scope
  void ReadData(float time, int ofdm_symbol_size, int num_symbols, int first_carrier_offset, int num_rb)
  {
    auto num_sc = num_rb * NR_NB_SC_PER_RB;
    if (!frozen || next) {
      if (scope_data->is_data_ready) {
        iq_procedure_timer.Add(scope_data->data.time_taken_in_ns);
        uint16_t first_sc = first_carrier_offset;
        uint16_t last_sc = first_sc + num_rb * NR_NB_SC_PER_RB;
        bool wrapped = false;
        uint16_t wrapped_first_sc = 0;
        uint16_t wrapped_last_sc = 0;
        if (last_sc >= ofdm_symbol_size) {
          last_sc = ofdm_symbol_size - 1;
          wrapped = true;
          auto num_sc_left = num_sc - (last_sc - first_sc + 1);
          wrapped_last_sc = wrapped_first_sc + num_sc_left - 1;
        }
        timestamp = time;
        scopeGraphData_t *iq_header = scope_data->data.scope_graph_data;
        len = iq_header->lineSz;
        c16_t *source = (c16_t *)(iq_header + 1);

        power.reserve(num_sc * num_symbols);
        for (auto symbol = 0; symbol < num_symbols; symbol++) {
          int subcarrier = 0;
          for (auto sc = first_sc; sc <= last_sc; sc++) {
            auto source_index = sc + symbol * ofdm_symbol_size;
            power[subcarrier * num_symbols + symbol] = std::pow(source[source_index].r, 2) + std::pow(source[source_index].i, 2);
            subcarrier++;
          }
          if (wrapped) {
            for (auto sc = wrapped_first_sc; sc <= wrapped_last_sc; sc++) {
              auto source_index = sc + symbol * ofdm_symbol_size;
              power[subcarrier * num_symbols + symbol] = std::pow(source[source_index].r, 2) + std::pow(source[source_index].i, 2);
              subcarrier++;
            }
          }
        }
        max = *std::max_element(power.begin(), power.end());
        if (frozen && max > stop_at_min) {
          next = false;
        }
        scope_data->is_data_ready = false;
      }
    }
  }
  void Draw(float time, int ofdm_symbol_size, int num_symbols, int first_carrier_offset, int num_rb)
  {
    ReadData(time, ofdm_symbol_size, num_symbols, first_carrier_offset, num_rb);
    ImGui::BeginGroup();
    if (ImGui::Button(frozen ? "Unfreeze" : "Freeze")) {
      frozen = !frozen;
      next = false;
    }
    if (frozen) {
      ImGui::SameLine();
      ImGui::BeginDisabled(next);
      if (ImGui::Button("Load next data")) {
        next = true;
      }
      ImGui::EndDisabled();
      ImGui::Text("Filter parameters:");
      ImGui::InputFloat("Max Power minimum", &stop_at_min, 10, 100);
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Data with maximum power below that value will be discarded.");
      }
    }
    static std::vector<int> symbol_boundaries = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
    const float heatmap_h = (float)ImGui::GetWindowWidth() * 0.9f;
    if (heatmap_h > 2.f && ImPlot::BeginPlot(label.c_str(), {heatmap_h, 0})) {
      auto num_sc = num_rb * NR_NB_SC_PER_RB;
      ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
      ImPlot::SetupAxes("symbols", "subcarriers");
      ImPlot::SetupAxisLimits(ImAxis_X1, num_symbols, 0);
      ImPlot::SetupAxisLimits(ImAxis_Y1, num_sc, 0);
      ImPlot::PlotHeatmap(label.c_str(),
                          power.data(),
                          num_sc,
                          num_symbols,
                          0,
                          max,
                          nullptr,
                          {0, 0},
                          {(double)num_symbols, (double)num_sc});
      ImPlot::PlotInfLines("Symbol boundary", symbol_boundaries.data(), symbol_boundaries.size());
      ImPlot::PlotInfLines("RB boundary", rb_boundaries.data(), num_rb, ImPlotInfLinesFlags_Horizontal);
      ImPlot::EndPlot();
    }
    ImGui::SameLine();
    ImPlot::ColormapScale("##HeatScale", 0, max);
    ImGui::Text("Data is %.2f seconds old", time - timestamp);
    ImGui::EndGroup();
  }
};

// utility structure for realtime plot
struct ScrollingBuffer {
  int MaxSize;
  int Offset;
  ImVector<ImVec2> Data;
  ScrollingBuffer(int max_size = 2000)
  {
    MaxSize = max_size;
    Offset = 0;
    Data.reserve(MaxSize);
  }
  void AddPoint(float x, float y)
  {
    if (Data.size() < MaxSize)
      Data.push_back(ImVec2(x, y));
    else {
      Data[Offset] = ImVec2(x, y);
      Offset = (Offset + 1) % MaxSize;
    }
  }
  void Erase()
  {
    if (Data.size() > 0) {
      Data.shrink(0);
      Offset = 0;
    }
  }
};

// CSI-RS channel buffer layout: [rx][port][sc], n_sc = ofdm_symbol_size + FILTER_MARGIN (see csi_rx.c)
#define CSIRS_FILTER_MARGIN 32

/* Return true if window title should be shown. filter==NULL or "" means show all. Else filter is comma-separated list of titles. */
static bool ImScopeShowWindow(const char *title, const char *filter)
{
  if (!filter || filter[0] == '\0')
    return true;
  std::string s(filter);
  size_t start = 0;
  while (start < s.size()) {
    size_t end = s.find(',', start);
    if (end == std::string::npos)
      end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\t'))
      start++;
    size_t trim_end = end;
    while (trim_end > start && (s[trim_end - 1] == ' ' || s[trim_end - 1] == '\t'))
      trim_end--;
    if (trim_end > start) {
      std::string token = s.substr(start, trim_end - start);
      if (token == title)
        return true;
    }
    start = end + 1;
  }
  return false;
}

/* Parse comma-separated filter into a list of window titles (trimmed). Used for initial dock layout. */
static void ImScopeParseFilterTitles(const char *filter, std::vector<std::string> &out_titles)
{
  out_titles.clear();
  if (!filter || filter[0] == '\0')
    return;
  std::string s(filter);
  size_t start = 0;
  while (start < s.size()) {
    size_t end = s.find(',', start);
    if (end == std::string::npos)
      end = s.size();
    while (start < end && (s[start] == ' ' || s[start] == '\t'))
      start++;
    size_t trim_end = end;
    while (trim_end > start && (s[trim_end - 1] == ' ' || s[trim_end - 1] == '\t'))
      trim_end--;
    if (trim_end > start) {
      out_titles.push_back(s.substr(start, trim_end - start));
    }
    start = end + 1;
  }
}

void ShowUeScope(void *data_void_ptr, float t, const char *window_filter)
{
  PHY_VARS_NR_UE *ue = (PHY_VARS_NR_UE *)data_void_ptr;

  static IQData *csirs_shared_iq_data = nullptr;
  static IQHist *csirs_iq_hist_flatten = nullptr;
  static bool read_csirs_perlink = false;
  if (!csirs_shared_iq_data) {
    csirs_shared_iq_data = new IQData();
    csirs_iq_hist_flatten = new IQHist("CSI-RS channel IQ");
  }

  if (ImScopeShowWindow("UE KPI", window_filter)) {
    ImGui::Begin("UE KPI");
    const ImVec2 kpi_avail = ImGui::GetContentRegionAvail();
    if (kpi_avail.y > 2.f && ImPlot::BeginPlot("##Scrolling", ImVec2(-1, 150))) {
      static float history = 10.0f;
      ImGui::SliderFloat("History", &history, 1, 30, "%.1f s");
      static ScrollingBuffer rbs_buffer;
      static ScrollingBuffer bler;
      static ScrollingBuffer mcs;
      rbs_buffer.AddPoint(t, getKPIUE()->nofRBs);
      bler.AddPoint(t, (float)getKPIUE()->nb_nack / (float)getKPIUE()->nb_total);
      mcs.AddPoint(t, (float)getKPIUE()->dl_mcs);
      ImPlot::SetupAxes(nullptr, nullptr, ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
      ImPlot::SetupAxes("time", "noOfRbs");
      ImPlot::SetupAxisLimits(ImAxis_X1, t - history, t, ImGuiCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, 0, NR_MAX_RB);
      ImPlot::SetupAxis(ImAxis_Y2, "bler [%]", ImPlotAxisFlags_AuxDefault);
      ImPlot::SetupAxis(ImAxis_Y3, "MCS", ImPlotAxisFlags_AuxDefault);
      ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
      ImPlot::PlotLine("noOfRbs", &rbs_buffer.Data[0].x, &rbs_buffer.Data[0].y, rbs_buffer.Data.size(), 0, 0, 2 * sizeof(float));
      ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
      ImPlot::PlotLine("bler", &bler.Data[0].x, &bler.Data[0].y, bler.Data.size(), 0, 0, 2 * sizeof(float));
      ImPlot::SetAxes(ImAxis_X1, ImAxis_Y3);
      ImPlot::PlotLine("mcs", &mcs.Data[0].x, &mcs.Data[0].y, mcs.Data.size(), 0, 0, 2 * sizeof(float));
      ImPlot::EndPlot();
    }
    ImGui::End();
  }

  if (ImScopeShowWindow("UE PDCCH IQ", window_filter)) {
    if (ImGui::Begin("UE PDCCH IQ")) {
      static auto iq_data = new IQData();
      static auto pdcch_iq_hist = new IQHist("PDCCH IQ");
      bool new_data = false;
      if (pdcch_iq_hist->ShouldReadData()) {
        new_data = iq_data->TryCollect(&scope_array[pdcchRxdataF_comp], t, pdcch_iq_hist->GetEpsilon(), iq_procedure_timer);
      }
      pdcch_iq_hist->Draw(iq_data, t, new_data);
    }
    ImGui::End();
  }
  if (ImScopeShowWindow("UE PDCCH LLR", window_filter)) {
    if (ImGui::Begin("UE PDCCH LLR")) {
      static auto llr_plot = new LLRPlot();
      llr_plot->Draw(t, pdcchLlr, "PDCCH LLR");
    }
    ImGui::End();
  }
  if (ImScopeShowWindow("UE PDSCH IQ", window_filter)) {
    if (ImGui::Begin("UE PDSCH IQ")) {
      static auto iq_data = new IQData();
      static auto pdsch_iq_hist = new IQHist("PDSCH IQ");
      bool new_data = false;
      if (pdsch_iq_hist->ShouldReadData()) {
        new_data = iq_data->TryCollect(&scope_array[pdschRxdataF_comp], t, pdsch_iq_hist->GetEpsilon(), iq_procedure_timer);
      }
      pdsch_iq_hist->Draw(iq_data, t, new_data);
    }
    ImGui::End();
  }
  if (ImScopeShowWindow("UE PDSCH Chan est", window_filter)) {
    if (ImGui::Begin("UE PDSCH Chan est")) {
      static auto iq_data = new IQData();
      static auto iq_hist = new IQHist("PDSCH Chan est IQ");
      bool new_data = false;
      if (iq_hist->ShouldReadData()) {
        new_data = iq_data->TryCollect(&scope_array[pdschChanEstimates], t, iq_hist->GetEpsilon(), iq_procedure_timer);
      }
      iq_hist->Draw(iq_data, t, new_data);
    }
    ImGui::End();
  }
  if (ImScopeShowWindow("UE PDSCH IQ before compensation", window_filter)) {
    if (ImGui::Begin("UE PDSCH IQ before compensation")) {
      static auto iq_data = new IQData();
      static auto iq_hist = new IQHist("PDSCH IQ before compensation");
      bool new_data = false;
      if (iq_hist->ShouldReadData()) {
        new_data = iq_data->TryCollect(&scope_array[pdschRxdataF], t, iq_hist->GetEpsilon(), iq_procedure_timer);
      }
      iq_hist->Draw(iq_data, t, new_data);
    }
    ImGui::End();
  }
  if (ImScopeShowWindow("UE CSI-RS channel estimates", window_filter)) {
    if (ImGui::Begin("UE CSI-RS channel estimates")) {
      bool new_data = false;
      if (csirs_iq_hist_flatten->ShouldReadData()) {
        new_data = csirs_shared_iq_data->TryCollect(&scope_array[ueCsirsChEstimate], t, csirs_iq_hist_flatten->GetEpsilon(), iq_procedure_timer);
      }
      csirs_iq_hist_flatten->Draw(csirs_shared_iq_data, t, new_data);
    }
    ImGui::End();
  }
  if (ImScopeShowWindow("UE CSI-RS channel estimates (per RX-TX link)", window_filter)) {
    if (ImGui::Begin("UE CSI-RS channel estimates (per RX-TX link)")) {
    if (read_csirs_perlink) {
      csirs_shared_iq_data->TryCollect(&scope_array[ueCsirsChEstimate], t, csirs_iq_hist_flatten->GetEpsilon(), iq_procedure_timer);
    }
    ImGui::Checkbox("Read", &read_csirs_perlink);
    const int n_sc = ue->frame_parms.ofdm_symbol_size + CSIRS_FILTER_MARGIN;
    const int nb_rx = ue->frame_parms.nb_antennas_rx;
    if (n_sc > 0 && nb_rx > 0 && csirs_shared_iq_data->len > 0) {
      const int num_links = csirs_shared_iq_data->len / n_sc;
      const int num_ports = num_links / nb_rx;
      if (num_ports > 0 && num_links <= 64) {
        const ImVec2 csirs_avail = ImGui::GetContentRegionAvail();
        if (csirs_avail.x > 2.f && csirs_avail.y > 2.f && ImPlot::BeginPlot("CSI-RS per link (RMS)")) {
          ImPlot::SetupAxes("Channel coefficient index", "RMS", ImPlotAxisFlags_AutoFit, ImPlotAxisFlags_AutoFit);
          const std::vector<float> &power = csirs_shared_iq_data->power;
          for (int link = 0; link < num_links; link++) {
            int rx = link / num_ports;
            int tx = link % num_ports;
            char label[32];
            snprintf(label, sizeof(label), "RX%d-TX%d", rx, tx);
            ImPlot::PlotLine(label, &power[link * n_sc], n_sc);
          }
          ImPlot::EndPlot();
        }
      } else {
        ImGui::Text("No data or too many links (num_links=%d)", num_links);
      }
    } else {
      ImGui::Text("Enable Read to show per RX-TX channel estimates (same data as UE CSI-RS channel estimates).");
    }
    }
    ImGui::End();
  }
  if (ImScopeShowWindow("Time domain samples", window_filter)) {
    if (ImGui::Begin("Time domain samples")) {
      static auto iq_data = new IQData();
      // Issue with imgui deferring draw calls until the end of the frame - cases segfault if scatterplot has too many points
      bool disable_scatterplot = true;
      static auto time_domain_iq = new IQHist("Time domain samples", disable_scatterplot);
      bool new_data = false;
      if (time_domain_iq->ShouldReadData()) {
        new_data = iq_data->TryCollect(&scope_array[ueTimeDomainSamples], t, time_domain_iq->GetEpsilon(), iq_procedure_timer);
      }
      time_domain_iq->Draw(iq_data, t, new_data);
    }
    ImGui::End();
  }
  if (ImScopeShowWindow("Time domain samples - before sync", window_filter)) {
    if (ImGui::Begin("Time domain samples - before sync")) {
      static auto iq_data = new IQData();
      // Issue with imgui deferring draw calls until the end of the frame - cases segfault if scatterplot has too many points
      bool disable_scatterplot = true;
      static auto time_domain_iq = new IQHist("Time domain samples - before sync", disable_scatterplot);
      bool new_data = false;
      if (time_domain_iq->ShouldReadData()) {
        new_data = iq_data->TryCollect(&scope_array[ueTimeDomainSamplesBeforeSync], t, time_domain_iq->GetEpsilon(), iq_procedure_timer);
      }
      time_domain_iq->Draw(iq_data, t, new_data);
    }
    ImGui::End();
  }
  if (ImScopeShowWindow("Broadcast channel", window_filter)) {
    if (ImGui::Begin("Broadcast channel")) {
    ImGui::Text("RSRP %d", ue->measurements.ssb_rsrp_dBm[ue->frame_parms.ssb_index]);
    if (ImGui::TreeNode("IQ")) {
      static auto iq_data = new IQData();
      static auto broadcast_iq_hist = new IQHist("Broadcast IQ");
      bool new_data = false;
      if (broadcast_iq_hist->ShouldReadData()) {
        new_data = iq_data->TryCollect(&scope_array[ue->sl_mode ? psbchRxdataF_comp : pbchRxdataF_comp],
                                       t,
                                       broadcast_iq_hist->GetEpsilon(), iq_procedure_timer);
      }
      broadcast_iq_hist->Draw(iq_data, t, new_data);
      ImGui::TreePop();
    }
    if (ImGui::TreeNode("CHest")) {
      static auto chest_iq_data = new IQData();
      static auto broadcast_iq_chest = new IQHist("Broadcast Chest");
      bool new_data = false;
      if (broadcast_iq_chest->ShouldReadData()) {
        new_data = chest_iq_data->TryCollect(&scope_array[ue->sl_mode ? psbchDlChEstimateTime : pbchDlChEstimateTime],
                                             t,
                                             broadcast_iq_chest->GetEpsilon(), iq_procedure_timer);
      }
      broadcast_iq_chest->Draw(chest_iq_data, t, new_data);
      ImGui::TreePop();
    }

    if (ImGui::TreeNode("LLR")) {
      static auto llr_plot = new LLRPlot();
      llr_plot->Draw(t, ue->sl_mode ? psbchLlr : pbchLlr, "Broadcast LLR");
      ImGui::TreePop();
    }
    }
    ImGui::End();
  }

  // if (ImGui::Begin("RX IQ")) {
  //   static auto common_rx_iq_heatmap = new IQSlotHeatmap(&scope_array[commonRxdataF], "common RX IQ");
  //   common_rx_iq_heatmap->Draw(t,
  //                              ue->frame_parms.ofdm_symbol_size,
  //                              ue->frame_parms.symbols_per_slot,
  //                              ue->frame_parms.first_carrier_offset,
  //                              ue->frame_parms.N_RB_DL);
  // }
  // ImGui::End();
}

void ShowGnbScope(void *data_void_ptr, float t, const char *window_filter)
{
  scopeParms_t *scope_params = (scopeParms_t *)data_void_ptr;
  PHY_VARS_gNB *gNB = scope_params ? scope_params->gNB : nullptr;
  static IQData *srs_shared_iq_data = nullptr;
  static IQHist *srs_iq_hist_flatten = nullptr;
  static bool read_srs_perlink = false;
  /* Snapshot of SRS per-link RMS: refreshed only when new gNBSrsChEstimate data is recorded (Read on). Plot always uses this cache. */
  static std::vector<float> srs_perlink_plot_cache;
  static int srs_perlink_cached_n_per_link = 0;
  static int srs_perlink_cached_num_links = 0;
  static int srs_perlink_cached_n_ap = 0;
  static bool srs_perlink_plot_has_snapshot = false;
  static metadata srs_perlink_plot_meta = {-1, -1};
  static float srs_perlink_plot_ymax = 1.f;
  if (!srs_shared_iq_data) {
    srs_shared_iq_data = new IQData();
    srs_iq_hist_flatten = new IQHist("SRS channel IQ");
  }

  /* Single TryCollect per frame for gNBSrsChEstimate: both SRS windows share one IQData buffer; the scope slot
   * only signals is_data_ready once per PHY feed — a second TryCollect in the per-link window always failed. */
  const bool win_srs_flat = ImScopeShowWindow("SRS channel estimates", window_filter);
  const bool win_srs_perlink = ImScopeShowWindow("SRS channel estimates (per RX-port link)", window_filter);
  bool srs_scope_new_data = false;
  if (scope_array[gNBSrsChEstimate].is_data_ready) {
    const bool want_srs_collect = (win_srs_flat && srs_iq_hist_flatten->ShouldReadData())
                                  || (win_srs_perlink && read_srs_perlink);
    if (want_srs_collect) {
      srs_scope_new_data =
          srs_shared_iq_data->TryCollect(&scope_array[gNBSrsChEstimate], t, srs_iq_hist_flatten->GetEpsilon(), iq_procedure_timer);
    }
  }
  // if (ImGui::TreeNode("RX IQ")) {
  //   static auto gnb_heatmap = new IQSlotHeatmap(&scope_array[gNBRxdataF], "common RX IQ");

  //   gnb_heatmap->Draw(t,
  //                     gNB->frame_parms.ofdm_symbol_size,
  //                     gNB->frame_parms.symbols_per_slot,
  //                     gNB->frame_parms.first_carrier_offset,
  //                     gNB->frame_parms.N_RB_UL);
  //   ImGui::TreePop();
  // }
  if (ImScopeShowWindow("PUSCH SLOT IQ", window_filter)) {
    if (ImGui::Begin("PUSCH SLOT IQ")) {
      static auto pusch_iq = new IQData();
      static auto pusch_iq_display = new IQHist("PUSCH compensated IQ");
      bool new_data = false;
      if (pusch_iq_display->ShouldReadData()) {
        new_data = pusch_iq->TryCollect(&scope_array[gNBPuschRxIq], t, pusch_iq_display->GetEpsilon(), iq_procedure_timer);
      }
      pusch_iq_display->Draw(pusch_iq, t, new_data);
    }
    ImGui::End();
  }
  if (ImScopeShowWindow("PUSCH LLRs", window_filter)) {
    if (ImGui::Begin("PUSCH LLRs")) {
      static auto pusch_llr_plot = new LLRPlot();
      pusch_llr_plot->Draw(t, gNBPuschLlr, "PUSCH LLR");
    }
    ImGui::End();
  }
  if (ImScopeShowWindow("Time domain samples", window_filter)) {
    if (ImGui::Begin("Time domain samples")) {
      static auto iq_data = new IQData();
      // Issue with imgui deferring draw calls until the end of the frame - cases segfault if scatterplot has too many points
      bool disable_scatterplot = true;
      static auto time_domain_iq = new IQHist("Time domain samples", disable_scatterplot);
      bool new_data = false;
      if (time_domain_iq->ShouldReadData()) {
        new_data = iq_data->TryCollect(&scope_array[gNbTimeDomainSamples], t, time_domain_iq->GetEpsilon(), iq_procedure_timer);
      }
      time_domain_iq->Draw(iq_data, t, new_data);
    }
    ImGui::End();
  }
  if (win_srs_flat) {
    if (ImGui::Begin("SRS channel estimates")) {
      srs_iq_hist_flatten->Draw(srs_shared_iq_data, t, srs_scope_new_data);
    }
    ImGui::End();
  }
  if (win_srs_perlink) {
    if (ImGui::Begin("SRS channel estimates (per RX-port link)")) {
      ImGui::Checkbox("Read", &read_srs_perlink);
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("When enabled, record SRS scope samples into this window. The plot is updated only when new "
                          "SRS channel estimate data arrives; between updates the last snapshot is kept.");
      }
      const int n_sc = gNB ? gNB->frame_parms.ofdm_symbol_size : 0;
      int nb_rx = gNB ? gNB->frame_parms.nb_antennas_rx : 0;
      int n_ap = 0;
      int n_symb_srs = 0;
      if (gNB) {
        for (int i = 0; i < gNB->max_nb_srs; i++) {
          NR_gNB_SRS_t *srs = &gNB->srs[i];
          if (srs && srs->active) {
            n_ap = 1 << srs->srs_pdu.num_ant_ports;
            n_symb_srs = 1 << srs->srs_pdu.num_symbols;
            break;
          }
        }
      }
      /* Refresh snapshot only on new recorded SRS scope data while Read is on (plot draws from cache every frame). */
      if (read_srs_perlink && srs_scope_new_data && srs_shared_iq_data->len > 0 && n_sc > 0 && nb_rx > 0 && n_ap > 0
          && n_symb_srs > 0) {
        const int n_per_link = n_sc * n_symb_srs;
        const int num_links = srs_shared_iq_data->len / n_per_link;
        if (n_per_link > 0 && num_links == nb_rx * n_ap && num_links <= 64
            && (int)srs_shared_iq_data->power.size() == srs_shared_iq_data->len) {
          srs_perlink_plot_cache = srs_shared_iq_data->power;
          srs_perlink_cached_n_per_link = n_per_link;
          srs_perlink_cached_num_links = num_links;
          srs_perlink_cached_n_ap = n_ap;
          srs_perlink_plot_meta = srs_shared_iq_data->meta;
          float m = 0.f;
          for (float p : srs_perlink_plot_cache) {
            m = std::max(m, p);
          }
          srs_perlink_plot_ymax = std::max(m * 1.1f, 1e-6f);
          srs_perlink_plot_has_snapshot = true;
        } else {
          ImGui::TextColored(ImVec4(1.f, 0.6f, 0.2f, 1.f),
                             "Unexpected SRS size: len=%d (expected %d coeffs per link x %d links)",
                             srs_shared_iq_data->len,
                             n_per_link,
                             nb_rx * n_ap);
        }
      }

      if (srs_perlink_plot_has_snapshot && srs_perlink_cached_n_per_link > 0 && srs_perlink_cached_num_links > 0
          && (int)srs_perlink_plot_cache.size() >= srs_perlink_cached_num_links * srs_perlink_cached_n_per_link) {
        const ImVec2 srs_avail = ImGui::GetContentRegionAvail();
        if (srs_avail.x > 2.f && srs_avail.y > 2.f && ImPlot::BeginPlot("SRS per link (RMS)")) {
          ImPlot::SetupAxes("Channel coefficient index", "RMS", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
          ImPlot::SetupAxisLimits(ImAxis_X1,
                                  0,
                                  (double)std::max(0, srs_perlink_cached_n_per_link - 1),
                                  ImGuiCond_Always);
          ImPlot::SetupAxisLimits(ImAxis_Y1, 0, (double)srs_perlink_plot_ymax, ImGuiCond_Always);
          const int n_ap = srs_perlink_cached_n_ap;
          for (int link = 0; link < srs_perlink_cached_num_links; link++) {
            int rx = link / n_ap;
            int port = link % n_ap;
            char label[32];
            snprintf(label, sizeof(label), "RX%d-Port%d", rx, port);
            ImPlot::PlotLine(label,
                             srs_perlink_plot_cache.data() + link * srs_perlink_cached_n_per_link,
                             srs_perlink_cached_n_per_link);
          }
          ImPlot::EndPlot();
        }
        if (srs_perlink_plot_meta.slot != -1 || srs_perlink_plot_meta.frame != -1) {
          ImGui::Text("Snapshot: frame %d  slot %d  (updates only on new SRS estimate)",
                      srs_perlink_plot_meta.frame,
                      srs_perlink_plot_meta.slot);
        }
      } else if (!read_srs_perlink) {
        ImGui::TextUnformatted("Enable Read, then open \"SRS channel estimates\" or rely on this window to collect "
                               "when the flat window is hidden.");
      } else {
        ImGui::TextUnformatted("Waiting for first SRS channel estimate snapshot…");
      }
    }
    ImGui::End();
  }
  /* CSI report parameters (RI, PMI, CQI, RSRP, SINR) – data fed from MAC when a report is decoded */
  if (ImScopeShowWindow("CSI report parameters", window_filter)) {
    if (ImGui::Begin("CSI report parameters")) {
      static csi_report_scope_payload_t last_csi = {};
      static bool has_csi = false;
      ImScopeDataWrapper &w = scope_array[gNBCsiReportParams];
      if (w.is_data_ready && w.data.scope_graph_data != nullptr) {
        size_t payload_sz = sizeof(csi_report_scope_payload_t);
        if (w.data.scope_graph_data->dataSize >= (int)payload_sz) {
          memcpy(&last_csi, (const char *)(w.data.scope_graph_data + 1), payload_sz);
          has_csi = true;
        }
        w.is_data_ready = false;
      }
      if (has_csi) {
        ImGui::Text("Frame %d  Slot %d  RNTI 0x%04x", last_csi.frame, last_csi.slot, last_csi.rnti);
        /* RI is stored 0-based (0=1 layer, …, 3=4 layers); display as number of layers to match UE / MAC decode. */
        ImGui::Text("CSI RI %u (preferred DL layers)   CQI %u   PMI (x1=%u x2=%u)",
                    (unsigned)(last_csi.ri + 1u),
                    (unsigned)last_csi.cqi,
                    (unsigned)last_csi.pmi_x1,
                    (unsigned)last_csi.pmi_x2);
        ImGui::Text("Cell DL MIMO: max PDSCH layers %u   logical DL ports %u (when both are 4, 4-layer CSI/MIMO is configured)",
                    (unsigned)last_csi.max_dl_mimo_layers,
                    (unsigned)last_csi.pdsch_logical_ports);
        /* CQI report does not include SINR in UCI; UE only sends CQI. SINR is filled only when SINR report type is used. */
        if (last_csi.sinr_dB != 0) {
          ImGui::Text("RSRP %d dBm   SINR %d dB   Report ID %u", last_csi.rsrp_dBm, last_csi.sinr_dB, last_csi.csi_report_id);
        } else {
          ImGui::Text("RSRP %d dBm   SINR N/A (not in CQI report)   Report ID %u", last_csi.rsrp_dBm, last_csi.csi_report_id);
        }
        ImGui::Separator();
        ImGui::TextUnformatted("AI runtime CSI (MAC snapshot at this decoded report)");
        {
          const char *mode_str = "off";
          if (last_csi.ai_sched_mode == 1)
            mode_str = "1=bundled UL-SCH / PUCCH AI tuple";
          else if (last_csi.ai_sched_mode == 2)
            mode_str = "2=mode-2 (AI replaces legacy CSI payload)";
          ImGui::Text("Runtime sched mode: %u (%s)", (unsigned)last_csi.ai_sched_mode, mode_str);
        }
        ImGui::Text("AI tuple valid %u   fresh %u   last update frame %u slot %u",
                    (unsigned)last_csi.ai_runtime_tuple_valid,
                    (unsigned)last_csi.ai_runtime_tuple_fresh,
                    (unsigned)last_csi.ai_runtime_origin_frame,
                    (unsigned)last_csi.ai_runtime_origin_slot);
        if (last_csi.ai_runtime_age_slots == 0xFFFFu) {
          ImGui::TextUnformatted("Age since AI tuple update: N/A (no valid tuple)");
        } else {
          ImGui::Text("Age since AI tuple update: %u slots", (unsigned)last_csi.ai_runtime_age_slots);
        }
        ImGui::Text("Stored AI RI %u (layers)   CQI %u   PMI (x1=%u x2=%u)",
                    (unsigned)(last_csi.ai_runtime_ri + 1u),
                    (unsigned)last_csi.ai_runtime_cqi,
                    (unsigned)last_csi.ai_runtime_pmi_x1,
                    (unsigned)last_csi.ai_runtime_pmi_x2);
        {
          const bool tuple_valid = last_csi.ai_runtime_tuple_valid != 0;
          const char *ri_match = tuple_valid
                                     ? (last_csi.ai_ri_match_decode_vs_runtime == 1 ? "yes" : "no")
                                     : "N/A (AI tuple invalid)";
          const char *pmi_match = tuple_valid
                                      ? (last_csi.ai_pmi_match_decode_vs_runtime == 1 ? "yes" : "no")
                                      : "N/A (AI tuple invalid)";
          ImGui::Text("Decode vs AI runtime - RI match: %s   PMI match: %s", ri_match, pmi_match);
          if (tuple_valid) {
            ImGui::Text("CQI delta (AI minus decode): %d", (int)last_csi.ai_cqi_delta_ai_minus_decode);
          } else {
            ImGui::TextUnformatted("CQI delta (AI minus decode): N/A (AI tuple invalid)");
          }
        }
        if (last_csi.ai_runtime_override_disagrees_decode) {
          ImGui::TextColored(ImVec4(1.f, 0.85f, 0.2f, 1.f),
                             "Override active: fresh AI tuple differs from decoded CSI (DL uses AI per policy).");
        } else {
          ImGui::TextUnformatted("Override vs decode: no disagreement under current mode/freshness (or mode off).");
        }
        {
          const uint64_t override_used = (uint64_t)last_csi.ai_runtime_override_used;
          const uint64_t fb_missing = (uint64_t)last_csi.ai_runtime_fallback_missing;
          const uint64_t fb_stale = (uint64_t)last_csi.ai_runtime_fallback_stale;
          const uint64_t fb_incomplete = (uint64_t)last_csi.ai_runtime_fallback_incomplete;
          const uint64_t fallback_total = fb_missing + fb_stale + fb_incomplete;
          const uint64_t total_decisions = override_used + fallback_total;
          const double total_d = total_decisions > 0 ? (double)total_decisions : 1.0;
          const double fb_rate = total_decisions > 0 ? (100.0 * (double)fallback_total / total_d) : 0.0;
          const double miss_rate = total_decisions > 0 ? (100.0 * (double)fb_missing / total_d) : 0.0;
          const double stale_rate = total_decisions > 0 ? (100.0 * (double)fb_stale / total_d) : 0.0;
          const double incomplete_rate = total_decisions > 0 ? (100.0 * (double)fb_incomplete / total_d) : 0.0;
          ImGui::Text("Runtime decision samples: %llu", (unsigned long long)total_decisions);
          ImGui::Text("Override used: %u", (unsigned)last_csi.ai_runtime_override_used);
          ImGui::Text("Fallback total: %llu (%.2f%%) [missing %.2f%%, stale %.2f%%, incomplete %.2f%%]",
                      (unsigned long long)fallback_total,
                      fb_rate,
                      miss_rate,
                      stale_rate,
                      incomplete_rate);
        }
        ImGui::Text("Counters — override_used %u   fallback missing %u   stale %u   incomplete %u",
                    (unsigned)last_csi.ai_runtime_override_used,
                    (unsigned)last_csi.ai_runtime_fallback_missing,
                    (unsigned)last_csi.ai_runtime_fallback_stale,
                    (unsigned)last_csi.ai_runtime_fallback_incomplete);
        ImGui::Separator();
        ImGui::TextUnformatted("DL link quality (running BLER from gNB scheduler)");
        {
          /* Rolling history of DL BLER and DL throughput. We sample on every new CSI report (detected by a
           * change in the report's own frame/slot), not on AI runtime tuple updates — those only move when
           * --ai-fb-runtime-sched-mode != 0, which is off by default. Both curves share one write index so
           * they stay aligned in time. */
          static const int kHistN = 256;
          static const float kTputMaxMbps = 50.0f; /* fixed throughput y-axis top (Mbps); raise if DL iperf exceeds it */
          static float bler_hist[kHistN] = {0};
          static float tput_hist[kHistN] = {0}; /* DL MAC throughput in Mbps (raw, instantaneous) */
          static int hist_idx = 0;
          static int last_report_frame = -1;
          static int last_report_slot = -1;
          static uint64_t prev_dl_bytes = 0;
          static float prev_t = -1.0f;
          static float tput_ewma = 0.0f;
          static float last_tput_raw = 0.0f;
          if (last_csi.frame != last_report_frame || last_csi.slot != last_report_slot) {
            /* DL throughput = delta of the cumulative MAC TX byte counter over wall-clock time. t is the
             * scope's animation clock in seconds (t += ImGui DeltaTime each render frame). The byte counter
             * resets to a smaller value on UE re-attach, so guard against a negative delta. */
            if (prev_t >= 0.0f && t > prev_t && last_csi.dl_total_bytes >= prev_dl_bytes) {
              double mbps = (double)(last_csi.dl_total_bytes - prev_dl_bytes) * 8.0 / (double)(t - prev_t) / 1e6;
              last_tput_raw = (float)mbps;
              /* Light EWMA tames the per-sample jitter from detecting reports at GUI-frame granularity. */
              tput_ewma = 0.6f * tput_ewma + 0.4f * (float)mbps;
            }
            bler_hist[hist_idx] = last_csi.dl_bler;
            tput_hist[hist_idx] = last_tput_raw; /* plot the RAW instantaneous rate (not the EWMA) */
            hist_idx = (hist_idx + 1) % kHistN;
            prev_dl_bytes = last_csi.dl_total_bytes;
            prev_t = t;
            last_report_frame = last_csi.frame;
            last_report_slot = last_csi.slot;
          }
          ImGui::Text("Current DL BLER: %.4f   |   DL MCS: %u   |   Samples so far: %d",
                      last_csi.dl_bler,
                      (unsigned)last_csi.dl_mcs,
                      hist_idx);
          /* Fixed 0..1 y-axis (previously auto-scaled with FLT_MAX): BLER's absolute magnitude is the point,
           * so a fixed scale keeps it honest and comparable across runs / between CSI schemes. */
          ImGui::PlotLines("BLER",
                           bler_hist,
                           kHistN,
                           hist_idx,
                           nullptr,
                           0.0f,
                           1.0f,
                           ImVec2(0, 80));
          ImGui::TextDisabled("y: DL BLER (fixed 0.0-1.0)     x: CSI report index (last 256, oldest -> newest)");
          ImGui::Text("Current DL throughput: %.2f Mbps (smoothed)   |   %.2f Mbps (raw)   |   total DL TX: %.2f MB",
                      tput_ewma,
                      last_tput_raw,
                      (double)last_csi.dl_total_bytes / 1e6);
          /* Hover "(?)" to explain raw vs smoothed in-place, without cluttering the panel. */
          ImGui::SameLine();
          ImGui::TextDisabled("(?)");
          if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(
                "DL throughput is sampled once per CSI report:\n"
                "  (bytes sent since last report) x 8 / (seconds since last report).\n"
                "\n"
                "raw      = the instantaneous value; jumpy, because the time between\n"
                "           reports is uneven (like a speedometer read every instant).\n"
                "smoothed = a running average (EWMA 0.6*old + 0.4*new), shown as a\n"
                "           number for reference -- the RAW value is what is plotted.\n"
                "\n"
                "Both are in Mbps (MAC TX bytes, so retransmissions are included).\n"
                "raw and smoothed far apart => bursty link; close => steady.");
            ImGui::EndTooltip();
          }
          /* Throughput y-axis: FIXED 0..kTputMaxMbps so the scale is stable and comparable across runs
           * (was auto-scaled with FLT_MAX, which magnified idle noise); values above the top are clipped. */
          ImGui::PlotLines("DL Mbps (raw)",
                           tput_hist,
                           kHistN,
                           hist_idx,
                           nullptr,
                           0.0f,
                           kTputMaxMbps,
                           ImVec2(0, 80));
          ImGui::TextDisabled("y: DL throughput (fixed 0-%g Mbps)     x: CSI report index (last 256, oldest -> newest)", (double)kTputMaxMbps);
        }
      } else {
        ImGui::TextUnformatted("No CSI report received yet.");
      }
    }
    ImGui::End(); /* always call: ImGui requires End() for every Begin(), even when Begin() returned false (e.g. window collapsed/moved) */
  }
}

void ShowIQFileViewer(void *data_void_ptr)
{
  auto iq_data = static_cast<std::vector<IQData> *>(data_void_ptr);
  if (ImGui::Begin("Scope selection")) {
    static int selected_scope = 0;
    ImGui::Combo(
        "Select scope",
        &selected_scope,
        [](void *userdata, int idx) {
          std::vector<IQData> *iq_data = static_cast<std::vector<IQData>*>(userdata);
          return scope_id_to_string(static_cast<scopeDataType>((*iq_data)[idx].scope_id));
        },
        iq_data,
        iq_data->size());
    static auto iq_display = new IQHist("IQ File Viewer");
    iq_display->Draw(&(*iq_data)[selected_scope], 0, false);
  }
  ImGui::End();
}

void *imscope_thread(void *data_void_ptr)
{
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit())
    return nullptr;

    // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
  // GL ES 2.0 + GLSL 100
  const char *glsl_version = "#version 100";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#elif defined(__APPLE__)
  // GL 3.2 + GLSL 150
  const char *glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // 3.2+ only
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on Mac
#else
  // GL 3.0 + GLSL 130
  const char *glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
  // glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
  // glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
#endif

  // Create window with graphics context
  GLFWwindow *window = glfwCreateWindow(1280, 720, "imscope", nullptr, nullptr);
  if (window == nullptr)
    return nullptr;
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1); // For frame capping

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  // Setup Dear ImGui style
  ImGui::StyleColorsDark();
  // ImGui::StyleColorsLight();

  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(window, true);
#ifdef __EMSCRIPTEN__
  ImGui_ImplGlfw_InstallEmscriptenCallbacks(window, "#canvas");
#endif
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Our state
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  for (auto i = 0U; i < NR_MAX_RB; i++) {
    rb_boundaries.push_back(i * NR_NB_SC_PER_RB);
  }

  static double last_frame_time = glfwGetTime();
  static int target_fps = 24;

  bool is_ue = IS_SOFTMODEM_5GUE;
  bool is_gnb = IS_SOFTMODEM_GNB;
  bool close_window = false;
  while (!glfwWindowShouldClose(window) && close_window == false) {
    // Poll and handle events (inputs, window resize, etc.)
    // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
    // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy
    // of the mouse data.
    // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your
    // copy of the keyboard data. Generally you may always pass all inputs to dear imgui, and hide them from your application based
    // on those two flags.
    glfwPollEvents();

    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();

    static bool reset_ini_settings = false;
    static bool filtered_layout_applied = false;
    if (reset_ini_settings) {
      ImGui::LoadIniSettingsFromDisk("imscope-init.ini");
      reset_ini_settings = false;
      filtered_layout_applied = false; /* re-apply filtered dock layout next frame if --imscope-windows is set */
    }
    ImGui::NewFrame();

    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);

    static float t = 0;
    static bool show_imgui_demo_window = false;
    static bool show_implot_demo_window = false;
    static bool show_scope_settings_window = false;
    ImGuiID dockspace_id = ImGui::DockSpaceOverViewport();

    const char *window_filter = nullptr;
    if (data_void_ptr) {
      if (is_ue) {
        PHY_VARS_NR_UE *ue = (PHY_VARS_NR_UE *)data_void_ptr;
        if (ue->scopeData) {
          scopeData_t *scope = (scopeData_t *)ue->scopeData;
          window_filter = scope->imscope_windows;
        }
      } else if (is_gnb) {
        scopeParms_t *parms = (scopeParms_t *)data_void_ptr;
        if (parms->gNB && parms->gNB->scopeData) {
          scopeData_t *scope = (scopeData_t *)parms->gNB->scopeData;
          window_filter = scope->imscope_windows;
        }
      }
    }

    /* When --imscope-windows is set, dock filtered windows into the main area so they appear as tabs at startup */
    if (window_filter && window_filter[0] != '\0' && !filtered_layout_applied) {
      filtered_layout_applied = true;
      std::vector<std::string> titles;
      ImScopeParseFilterTitles(window_filter, titles);
      if (!titles.empty()) {
        for (const std::string &title : titles)
          ImGui::DockBuilderDockWindow(title.c_str(), dockspace_id);
        ImGui::DockBuilderDockWindow("Status bar", dockspace_id);
        ImGui::DockBuilderFinish(dockspace_id);
      }
    }

    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Close scope")) {
          close_window = true;
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Options")) {
        ImGui::Checkbox("Show imgui demo window", &show_imgui_demo_window);
        ImGui::Checkbox("Show implot demo window", &show_implot_demo_window);
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Layout")) {
        if (ImGui::MenuItem("Reset")) {
          reset_ini_settings = true;
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Settings")) {
        if (ImGui::MenuItem("Global scope settings")) {
          show_scope_settings_window = !show_scope_settings_window;
        }
        ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
    }

    ImGui::Begin("Status bar");
    ImGui::Text("Total time used by IQ capture procedures per milisecond: %.2f [us]/[ms]", iq_procedure_timer.average / 1000);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Total time used in PHY threads for copying out IQ data for the scope, in uS, averaged over 1 ms");
    }
    if (window_filter && window_filter[0] != '\0') {
      ImGui::SameLine();
      ImGui::Text("| Windows: %s (--imscope-windows)", window_filter);
    } else {
      ImGui::SameLine();
      ImGui::Text("| Windows: all");
    }
    ImGui::End();

    t += ImGui::GetIO().DeltaTime;
    iq_procedure_timer.UpdateAverage(t);

    if (is_ue) {
      ShowUeScope(data_void_ptr, t, window_filter);
    } else if (is_gnb) {
      ShowGnbScope(data_void_ptr, t, window_filter);
    } else {
      ShowIQFileViewer(data_void_ptr);
    }

    // For reference
    if (show_implot_demo_window)
      ImPlot::ShowDemoWindow();
    if (show_imgui_demo_window)
      ImGui::ShowDemoWindow();
    // Settings
    if (show_scope_settings_window) {
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
      ImGui::Begin("Global scope settings", &show_scope_settings_window);
      ImGui::ShowStyleSelector("ImGui Style");
      ImPlot::ShowStyleSelector("ImPlot Style");
      ImPlot::ShowColormapSelector("ImPlot Colormap");
      ImGui::SliderInt("FPS target", &target_fps, 12, 60);
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Reduces scope flickering in unfrozen mode. Can reduce impact on perfromance of the modem");
      }
      ImGui::End();
    }
    // Rendering
    ImGui::Render();
    glViewport(0, 0, display_w, display_h);
    glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    double target_frame_time = 1.0 / target_fps;
    double delta = glfwGetTime() - last_frame_time;
    if (delta < target_frame_time) {
      std::this_thread::sleep_for(std::chrono::duration<float>(target_frame_time - delta));
    }

    glfwSwapBuffers(window);
    last_frame_time = glfwGetTime();
  }

  // Cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return nullptr;
}

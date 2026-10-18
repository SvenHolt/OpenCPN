// Offline OSM regions: area selection toolbar, drawer page and a chart
// layer with harbour details queried from the local Overpass instance.
#pragma once

#include <QTimer>
#include <QWidget>

#include "core/osm/Overpass.h"
#include "ui/GlassPanel.h"
#include "ui/MapLayer.h"

class ChartView;
class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

/** Toolbar shown while the skipper frames an area on the chart. */
class AreaSelectBar : public GlassPanel {
  Q_OBJECT
public:
  enum Preset { Visible, AroundBoat, AlongRoute };

  explicit AreaSelectBar(QWidget* parent = nullptr);
  void setArea(const GeoBox& box);
  /** Clears the name and enables the presets that apply. */
  void reset();
  QString regionName() const;

signals:
  void presetRequested(int preset);
  void cancel();
  void accepted();

private:
  QLabel* m_size;
  QLabel* m_hint;
  QLineEdit* m_name;
  QPushButton* m_route;
  QPushButton* m_ok;
};

/** Harbours, marinas, fuel, slipways and seamarks from OSM. */
class OsmPoiLayer : public MapLayer {
  Q_OBJECT
public:
  explicit OsmPoiLayer(ChartView* chart, QObject* parent = nullptr);
  QString name() const override { return tr("OSM details"); }
  void paint(QPainter& p, const ChartView& view) override;
  int zOrder() const override { return -2; }

  /** The Overpass QL query used for a bounding box. */
  static QString queryFor(const GeoBox& box);
  enum class Kind { Marina, Harbour, Anchorage, Fuel, Slipway, Seamark, Other };
  static Kind classify(const OsmElement& e);
  int elementCount() const { return int(m_elements.size()); }

private:
  void refresh();

  ChartView* m_chart;
  QTimer m_debounce;
  QVector<OsmElement> m_elements;
  GeoBox m_loaded;
  bool m_running = false, m_again = false;
};

class OfflinePage : public QWidget {
  Q_OBJECT
public:
  explicit OfflinePage(ChartView* chart, OsmPoiLayer* layer,
                       QWidget* parent = nullptr);

signals:
  void selectAreaRequested();

private:
  void rebuild();
  void updateStatus();
  void runSearch();
  void loadSettings();
  void applySettings();

  ChartView* m_chart;
  OsmPoiLayer* m_layer;
  QLabel* m_backendState;
  QPushButton* m_select;
  QPushButton* m_cancel;
  QProgressBar* m_progress;
  QLabel* m_status;
  QVBoxLayout* m_regions;
  QLabel* m_empty;
  QLineEdit* m_search;
  QVBoxLayout* m_results;
  QWidget* m_settings;
  QComboBox* m_backend;
  QLineEdit* m_binDir;
  QLineEdit* m_httpUrl;
  QLineEdit* m_downloadUrl;
};

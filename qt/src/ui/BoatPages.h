// Settings pages for the on-board systems: light scenes (which LED shines
// with which brightness and color) and automations (actions on a tap or at
// a time of day).
#pragma once

#include <QHash>
#include <QWidget>

#include "core/Automation.h"
#include "core/BoatSystems.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QStackedWidget;
class QTimeEdit;
class QVBoxLayout;
class ToggleSwitch;

/** Round color swatch; a click opens presets and a color dialog. */
class ColorButton : public QWidget {
  Q_OBJECT
public:
  explicit ColorButton(QWidget* parent = nullptr);
  QColor color() const { return m_color; }
  void setColor(const QColor& c);
  QSize sizeHint() const override { return {34, 34}; }
  /** LED colors offered in the menu. */
  static QVector<QPair<QString, QColor>> presets();

signals:
  void colorChanged(const QColor& c);

protected:
  void paintEvent(QPaintEvent*) override;
  void mouseReleaseEvent(QMouseEvent*) override;

private:
  QColor m_color;
};

/** Edits one scene: per light whether it belongs to the scene, on/off,
 *  brightness and color. With preview on, the lights follow the editor. */
class SceneEditor : public QWidget {
  Q_OBJECT
public:
  explicit SceneEditor(QWidget* parent = nullptr);
  void edit(const LightScene& scene);
  LightScene scene() const;
  /** Sets every included light to one color (and keeps the levels). */
  void setAllColors(const QColor& c);

signals:
  void saved(const QString& id);
  void closed();

private:
  struct Row {
    QString lightId;
    QCheckBox* include;
    ToggleSwitch* on;
    QSlider* level;
    QLabel* percent;
    ColorButton* color;
  };
  void preview();
  void finish();
  LightScene m_scene;
  QLineEdit* m_name;
  QComboBox* m_icon;
  QCheckBox* m_preview;
  QPushButton* m_delete;
  QVBoxLayout* m_rows;
  QVector<Row> m_lightRows;
  QVector<BoatLight> m_before;  // light states to restore after a preview
  bool m_loading = false;
};

class LightScenesPage : public QWidget {
  Q_OBJECT
public:
  explicit LightScenesPage(QWidget* parent = nullptr);
  SceneEditor* editor() const { return m_editor; }
  /** Opens the editor for a scene (empty id: new scene from the current
   *  lights). */
  void editScene(const QString& id);

private:
  void rebuild();
  QStackedWidget* m_stack;
  QVBoxLayout* m_list;
  SceneEditor* m_editor;
};

/** Edits one automation: name, trigger, weekdays, months and actions. */
class AutomationEditor : public QWidget {
  Q_OBJECT
public:
  explicit AutomationEditor(QWidget* parent = nullptr);
  void edit(const Automation& a);
  Automation automation() const;
  /** Appends an action row (tests and the "Add action" button). */
  void addAction(const AutomationAction& a);

signals:
  void saved(const QString& id);
  void closed();

private:
  struct ActionRow {
    QWidget* widget;
    QComboBox* type;
    QComboBox* target;
    QDoubleSpinBox* value;
    ToggleSwitch* on;
  };
  void fillTargets(ActionRow& row, AutomationAction::Type t,
                   const QString& current);
  void updateTrigger();
  Automation m_automation;
  QLineEdit* m_name;
  QComboBox* m_icon;
  QComboBox* m_trigger;
  QWidget* m_timeBox;
  QTimeEdit* m_time;
  QPushButton* m_days[7];
  QPushButton* m_months[12];
  QPushButton* m_delete;
  QVBoxLayout* m_actions;
  QVector<ActionRow> m_actionRows;
};

class AutomationsPage : public QWidget {
  Q_OBJECT
public:
  explicit AutomationsPage(QWidget* parent = nullptr);
  AutomationEditor* editor() const { return m_editor; }
  /** Opens the editor (empty id: new automation). */
  void editAutomation(const QString& id);

private:
  void rebuild();
  QStackedWidget* m_stack;
  QVBoxLayout* m_list;
  AutomationEditor* m_editor;
};

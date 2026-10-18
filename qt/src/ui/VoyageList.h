// Stored voyages (offline): list with status, open, refresh and delete.
#pragma once

#include <QWidget>

class QVBoxLayout;

class VoyageList : public QWidget {
  Q_OBJECT
public:
  explicit VoyageList(QWidget* parent = nullptr);

signals:
  void openRequested(const QString& id);

private:
  void rebuild();
  QVBoxLayout* m_rows;
  QString m_progressId, m_progressText;
};

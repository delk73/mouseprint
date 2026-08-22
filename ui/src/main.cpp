#include "inspection_controller.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

std::string default_database_path() {
  const char* state_home = std::getenv("XDG_STATE_HOME");
  if (state_home && *state_home) {
    return std::string(state_home) + "/mouseprint/mouseprint.sqlite3";
  }
  const char* home = std::getenv("HOME");
  if (home && *home) return std::string(home) + "/.local/state/mouseprint/mouseprint.sqlite3";
  return "mouseprint.sqlite3";
}

bool parse_database_path(int argc, char** argv, std::string& path) {
  path = default_database_path();
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--database" && index + 1 < argc) {
      path = argv[++index];
      continue;
    }
    std::cerr << "usage: " << argv[0] << " [--database PATH]\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string database_path;
  if (!parse_database_path(argc, argv, database_path)) return 2;

  QGuiApplication application(argc, argv);
  InspectionController controller;
  controller.load(QString::fromStdString(database_path));

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("inspection"), &controller);
  const QString qml_path = QCoreApplication::applicationDirPath() +
                           QStringLiteral("/../ui/qml/Main.qml");
  engine.load(QUrl::fromLocalFile(qml_path));
  if (engine.rootObjects().isEmpty()) {
    std::cerr << "mouseprint-inspector: could not load " << qml_path.toStdString() << "\n";
    return 1;
  }
  return application.exec();
}

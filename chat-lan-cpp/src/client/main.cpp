#include "window.h"
#include "common/paths.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QTextStream>
#include <QMessageBox>

// Inicia el cliente gráfico o imprime la clave pública para el modo de recuperación.
int main(int argc,char **argv) {
    // Public-key mode does not require a display server.
    bool keyMode=false; for(int i=1;i<argc;++i) if(QString::fromLocal8Bit(argv[i])=="--public-key") keyMode=true;
    if(keyMode) {
        QCoreApplication app(argc,argv); QCommandLineParser p; p.addHelpOption(); p.addOptions({{"profile","Carpeta de perfil","ruta",chat::dataRoot()+"/client"},{"public-key","Mostrar pública"}}); p.process(app);
        try { chat::Identity identity(p.value("profile")); QTextStream(stdout)<<identity.publicKey()<<'\n'; return 0; } catch(const std::exception &e) { QTextStream(stderr)<<e.what()<<'\n'; return 1; }
    }
    QApplication app(argc,argv); app.setApplicationName("Chat LAN"); app.setDesktopFileName("chat-lan-cpp"); app.setWindowIcon(QIcon(":/logo.png")); app.setStyleSheet(chat::theme());
    QCommandLineParser parser; parser.addHelpOption(); parser.addOption({"profile","Carpeta de perfil","ruta"}); parser.process(app);
    try {
        auto profile=parser.value("profile"); if(profile.isEmpty()) profile=chat::chooseProfile(); if(profile.isEmpty()) return 0;
        chat::Window window(profile); window.show(); return app.exec();
    } catch(const std::exception &e) { QMessageBox::critical(nullptr,"Perfil o configuración",e.what()); return 1; }
}

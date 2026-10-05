#include "server.h"
#include "common/paths.h"
#include "common/crypto.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <csignal>

static volatile std::sig_atomic_t stopRequested = 0;
static void signalHandler(int) { stopRequested = 1; }
int main(int argc, char **argv) {
    QCoreApplication app(argc,argv); QCoreApplication::setApplicationName("Chat LAN C++ Server");
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOptions({{"data","Directorio aislado del servidor","ruta",chat::defaultServerDir()},{"host","Dirección de escucha","ip"},{"port","Puerto TCP","puerto"},{"discovery-port","Puerto UDP","puerto"},{"allow","auto o lista CIDR","red"},{"init","Crear TLS si falta"}});
    parser.process(app);
    try {
        chat::ServerOptions options; options.directory = parser.value("data");
        auto cfg = chat::readJson(options.directory + "/settings.json");
        options.host = parser.isSet("host") ? parser.value("host") : cfg["host"].toString("0.0.0.0");
        auto port = parser.isSet("port") ? parser.value("port").toInt() : cfg["port"].toInt(8765);
        auto udp = parser.isSet("discovery-port") ? parser.value("discovery-port").toInt() : cfg["discovery_port"].toInt(8764);
        if (port < 1 || port > 65535 || udp < 1 || udp > 65535) throw std::runtime_error("Puerto inválido");
        options.port = port; options.discoveryPort = udp;
        options.allow = parser.isSet("allow") ? parser.value("allow") : cfg["allow"].toString("auto");
        options.autoApprove = cfg["auto_approve"].toBool(true);
        if (parser.isSet("init") && !QFile::exists(options.directory + "/server.pem")) chat::initializeTls(options.directory);
        chat::ChatServer server(options); server.start();
        std::signal(SIGINT,signalHandler); std::signal(SIGTERM,signalHandler);
        QTimer timer; timer.setInterval(100); QObject::connect(&timer,&QTimer::timeout,&app,[&] { if(stopRequested) { server.stop(); app.quit(); } }); timer.start();
        QObject::connect(&app,&QCoreApplication::aboutToQuit,&server,&chat::ChatServer::stop);
        return app.exec();
    } catch (const std::exception &e) { QTextStream(stderr) << "No se pudo iniciar: " << e.what() << '\n'; return 1; }
}

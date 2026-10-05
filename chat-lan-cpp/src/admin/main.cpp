#include "common/store.h"
#include "common/crypto.h"
#include "common/paths.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTextStream>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QCommandLineParser p; p.addHelpOption();
    p.addOptions({{"data","Directorio del servidor","ruta",chat::defaultServerDir()},{"json","Listado JSON"}});
    p.addPositionalArgument("acción","init, fingerprint, list, approve, revoke, disconnect, delete, recover o backup");
    p.addPositionalArgument("argumentos","UUID, nueva pública o destino según acción","[argumentos...]"); p.process(app);
    auto args=p.positionalArguments(); if(args.isEmpty()) p.showHelp(1);
    try {
        auto dir=p.value("data"); auto action=args.takeFirst();
        if(action=="init") { chat::initializeTls(dir); chat::Store store(dir+"/clients.sqlite3"); QTextStream(stdout)<<chat::certificateFingerprint(dir+"/server.pem")<<'\n'; }
        else if(action=="fingerprint") QTextStream(stdout)<<chat::certificateFingerprint(dir+"/server.pem")<<'\n';
        else {
            chat::Store store(dir+"/clients.sqlite3");
            if(action=="list") {
                if(p.isSet("json")) { QJsonArray users; for(auto u:store.clients()) users.append(u); QTextStream(stdout)<<QJsonDocument(users).toJson(); }
                else for(auto u:store.clients()) QTextStream(stdout)<<u["id"].toString()<<"  "<<QString::number(u["code"].toInt()).rightJustified(3,'0')<<"  "<<u["status"].toString()<<"  "<<(u["online"].toInt()?"online ":"offline ")<<u["name"].toString()<<'\n';
            } else {
                if(args.isEmpty()) throw std::runtime_error("Falta UUID o destino");
                if(action=="approve"||action=="revoke") store.setStatus(args[0],action=="approve"?"approved":"revoked");
                else if(action=="disconnect") store.disconnectClient(args[0]);
                else if(action=="delete") store.deleteClient(args[0]);
                else if(action=="recover") { if(args.size()!=2) throw std::runtime_error("recover exige UUID y pública"); store.recover(args[0],args[1]); }
                else if(action=="backup") store.backup(args[0]);
                else throw std::runtime_error("Acción desconocida");
                QTextStream(stdout)<<"Operación guardada. Las acciones sobre sesiones se aplican al revisar el monitor.\n";
            }
        }
        return 0;
    } catch(const std::exception &e) { QTextStream(stderr)<<"Error: "<<e.what()<<'\n'; return 1; }
}

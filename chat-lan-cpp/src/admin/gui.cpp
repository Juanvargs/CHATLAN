#include "common/store.h"
#include "common/paths.h"
#include <QApplication>
#include <QMainWindow>
#include <QCommandLineParser>
#include <QTableWidget>
#include <QHeaderView>
#include <QTabWidget>
#include <QTextEdit>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QProcess>
#include <QTimer>
#include <QNetworkInterface>
#include <QJsonDocument>
#include <QDateTime>

namespace {
const QString style=R"(
QWidget{background:#0f172a;color:#dbe5f2;font-size:14px;} QLabel{background:transparent;}
QPushButton{background:#3b638e;color:#edf4fc;border:0;border-radius:8px;padding:9px 14px;} QPushButton:hover{background:#4979a8;} QPushButton:disabled{background:#334155;color:#8294ab;}
QPushButton[danger="true"]{background:#80505e;}
QTableWidget,QTextEdit,QLineEdit,QSpinBox{background:#1a2638;border:1px solid #334155;border-radius:8px;color:#dbe5f2;padding:6px;}
QHeaderView::section{background:#26374d;padding:8px;border:0;color:#dbe5f2;} QTableWidget::item:selected{background:#365c82;}
QTabBar::tab{background:#1e293b;padding:10px 16px;} QTabBar::tab:selected{background:#365c82;} QTabWidget::pane{border:1px solid #334155;}
)";
class Admin : public QMainWindow {
    QString directory, unit, serviceState="consultando";
    bool goBackend=false;
    chat::Store store;
    QTableWidget *table;
    QLabel *summary, *feedback;
    QTextEdit *logs;
    QLineEdit *host,*allow;
    QSpinBox *port,*udp;
    QCheckBox *autoApprove;
    QPushButton *serviceToggle=nullptr, *accessButton=nullptr, *disconnectButton=nullptr, *deleteButton=nullptr, *saveSettingsButton=nullptr;
    QTimer refreshTimer;
    bool statusBusy=false,logsBusy=false,actionBusy=false;
    int onlineCount=0;
    QString selected() const { auto row=table->currentRow(); auto item=row<0?nullptr:table->item(row,0); return item?item->data(Qt::UserRole).toString():QString{}; }
    void updateButtons() {
        if(!serviceToggle || !accessButton || !disconnectButton || !deleteButton) return;
        const bool running=serviceState=="active";
        serviceToggle->setText(running?"Apagar servidor":"Encender servidor");
        serviceToggle->setEnabled(!actionBusy && serviceState!="consultando");
        if(saveSettingsButton) saveSettingsButton->setEnabled(!actionBusy);
        const auto row=table->currentRow();
        const auto item=row<0?nullptr:table->item(row,0);
        const auto status=item?item->data(Qt::UserRole+1).toString():QString{};
        const bool online=item && item->data(Qt::UserRole+2).toBool();
        accessButton->setText(status=="approved"?"Revocar acceso":status=="revoked"?"Reactivar acceso":"Aprobar usuario");
        accessButton->setEnabled(item!=nullptr);
        disconnectButton->setEnabled(online);
        deleteButton->setEnabled(item!=nullptr);
    }
    void process(const QString &program,const QStringList &args,const std::function<void(int,QString,QString)> &done,int timeout=15000) {
        auto p=new QProcess(this); auto timer=new QTimer(p); timer->setSingleShot(true);
        QObject::connect(timer,&QTimer::timeout,p,[p] { p->setProperty("timedOut",true); p->kill(); });
        QObject::connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[p,done](int code,QProcess::ExitStatus) {
            if(p->property("complete").toBool()) return; p->setProperty("complete",true);
            auto error=QString::fromUtf8(p->readAllStandardError()); if(p->property("timedOut").toBool()) error="La operación excedió el tiempo límite.";
            done(code,QString::fromUtf8(p->readAllStandardOutput()),error); p->deleteLater();
        });
        QObject::connect(p,&QProcess::errorOccurred,this,[p,done](QProcess::ProcessError error) {
            if(error!=QProcess::FailedToStart||p->property("complete").toBool()) return; p->setProperty("complete",true); done(-1,{},p->errorString()); p->deleteLater();
        });
        timer->start(timeout); p->start(program,args);
    }
    void updateSummary() {
        auto users=store.clients(); int online=0,approved=0;
        for(auto row:users) { online+=row["online"].toInt()!=0; approved+=row["status"]=="approved"; }
        onlineCount=serviceState=="active"?online:0;
        QStringList ips;
        for(auto interface:QNetworkInterface::allInterfaces()) if(interface.flags().testFlag(QNetworkInterface::IsUp)&&!interface.flags().testFlag(QNetworkInterface::IsLoopBack))
            for(auto entry:interface.addressEntries()) if(entry.ip().protocol()==QAbstractSocket::IPv4Protocol) ips.append(interface.name()+": "+entry.ip().toString());
        const auto state=serviceState=="active"?"En marcha":serviceState=="inactive"?"Apagado":serviceState=="failed"?"Error":serviceState;
        summary->setText(QString("Servidor: %1  ·  En línea: %2  ·  Autorizados: %3/10\nIP local: %4").arg(state).arg(onlineCount).arg(approved).arg(ips.join(" | ")));
        summary->setToolTip("Datos del servidor: "+directory);
        updateButtons();
    }
    void refresh() {
        try {
            auto id=selected(); auto users=store.clients(); table->clearSelection(); table->setRowCount(users.size());
            for(int r=0;r<users.size();++r) {
                auto u=users[r]; const auto rawStatus=u["status"].toString();
                auto status=rawStatus=="approved"?"Autorizado":rawStatus=="pending"?"Pendiente":rawStatus=="revoked"?"Revocado":rawStatus;
                auto last=u["last_seen"].toString();
                auto moment=QDateTime::fromString(last,"yyyy-MM-dd HH:mm:ss");
                if(moment.isValid()) { moment.setTimeSpec(Qt::UTC); last=moment.toLocalTime().toString("dd/MM/yyyy HH:mm"); }
                else last="Sin registro";
                const bool online=serviceState=="active" && u["online"].toInt()!=0;
                QStringList values{QString::number(u["code"].toInt()).rightJustified(3,'0'),u["name"].toString(),status,online?"En línea":"Desconectado",last};
                for(int c=0;c<values.size();++c) { auto item=new QTableWidgetItem(values[c]); item->setToolTip("ID: "+u["id"].toString()); table->setItem(r,c,item); }
                table->item(r,0)->setData(Qt::UserRole,u["id"].toString());
                table->item(r,0)->setData(Qt::UserRole+1,rawStatus);
                table->item(r,0)->setData(Qt::UserRole+2,online);
                if(u["id"]==id) table->selectRow(r);
            }
            updateButtons();
            updateSummary();
        } catch(const std::exception &e) { feedback->setText(e.what()); }
        if(!statusBusy) {
            statusBusy=true; process("systemctl",{"--user","is-active",unit},[this](int,QString out,QString error) { statusBusy=false; serviceState=out.trimmed(); if(serviceState.isEmpty()) serviceState=error; try { updateSummary(); } catch(...) {} },5000);
        }
        if(!logsBusy) {
            logsBusy=true; process("journalctl",{"--user","-u",unit,"-n","40","--no-pager"},[this](int,QString out,QString error) { logsBusy=false; logs->setPlainText(out.isEmpty()?error:out); },5000);
        }
    }
    void action(const QString &name,const QString &success) {
        if(actionBusy) return; actionBusy=true; updateButtons(); feedback->setText("Aplicando cambio…");
        process("systemctl",{"--user",name,unit},[this,name,success](int code,QString,QString error) {
            actionBusy=false;
            if(code==0) serviceState=name=="stop"?"inactive":"active";
            feedback->setText(code?(error.isEmpty()?"No se pudo cambiar el estado del servidor.":error):success); refresh();
        },20000);
    }
    void userAction(const QString &action) {
        auto id=selected(); if(id.isEmpty()) { QMessageBox::information(this,"Usuario","Selecciona un usuario."); return; }
        try {
            if(action=="delete") {
                if(QMessageBox::question(this,"Eliminar cuenta","¿Eliminar esta identidad y sus mensajes generales y privados asociados?\nEl perfil se limpia cuando el cliente actualizado recibe el aviso.")!=QMessageBox::Yes) return;
                store.deleteClient(id);
            } else if(action=="disconnect") store.disconnectClient(id); else store.setStatus(id,action);
            feedback->setText(action=="disconnect"?"La sesión se cerrará en unos segundos.":action=="delete"?"Cuenta eliminada o marcada para eliminar al desconectarse.":"Acceso actualizado."); refresh();
        } catch(const std::exception &e) { QMessageBox::warning(this,"Usuario",e.what()); }
    }
public:
    ~Admin() override {
        refreshTimer.stop();
        for(auto process:findChildren<QProcess*>()) {
            QObject::disconnect(process,nullptr,this,nullptr);
            if(process->state()!=QProcess::NotRunning) { process->kill(); process->waitForFinished(1000); }
        }
    }
    Admin(QString path,QString service,bool useGo) : directory(std::move(path)),unit(std::move(service)),goBackend(useGo),store(directory+"/clients.sqlite3") {
        setWindowTitle(goBackend?"Administrar Chat LAN Go":"Administrar Chat LAN C++"); setWindowIcon(QIcon(":/logo.png")); resize(1120,760);
        auto root=new QWidget; setCentralWidget(root); auto layout=new QVBoxLayout(root); layout->setContentsMargins(18,18,18,18);
        auto title=new QLabel(goBackend?"Servidor Chat LAN":"Servidor Chat LAN C++"); title->setStyleSheet("font-size:24px;font-weight:600"); layout->addWidget(title);
        summary=new QLabel; summary->setWordWrap(true); layout->addWidget(summary);
        auto row=new QHBoxLayout;
        serviceToggle=new QPushButton("Consultando servidor…"); serviceToggle->setObjectName("serviceToggle"); row->addWidget(serviceToggle);
        connect(serviceToggle,&QPushButton::clicked,this,[this] {
            if(serviceState=="active") {
                if(onlineCount>0 && QMessageBox::question(this,"Apagar servidor",QString("Hay %1 usuario(s) conectado(s). Se desconectarán al apagar el servidor. ¿Continuar?").arg(onlineCount))!=QMessageBox::Yes) return;
                action("stop","Servidor apagado.");
            }
            else action("start","Servidor encendido.");
        });
        auto refreshHint=new QLabel("Estado y usuarios se actualizan automáticamente."); refreshHint->setStyleSheet("color:#9fb2c8;font-size:12px;"); row->addWidget(refreshHint); row->addStretch(); layout->addLayout(row);
        feedback=new QLabel; feedback->setObjectName("feedback"); feedback->setWordWrap(true); layout->addWidget(feedback);
        auto tabs=new QTabWidget; layout->addWidget(tabs);
        auto users=new QWidget; auto usersLayout=new QVBoxLayout(users); table=new QTableWidget(0,5); table->setObjectName("usersTable"); table->setHorizontalHeaderLabels({"Código","Usuario","Acceso","Conexión","Último cambio"}); table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection); table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); table->horizontalHeader()->setStretchLastSection(true); table->horizontalHeaderItem(4)->setToolTip("Fecha local del último cambio de conexión; no es un historial completo."); usersLayout->addWidget(table);
        auto actions=new QHBoxLayout;
        accessButton=new QPushButton("Aprobar usuario"); accessButton->setObjectName("accessButton"); actions->addWidget(accessButton); connect(accessButton,&QPushButton::clicked,this,[this] {
            auto row=table->currentRow(); auto item=row<0?nullptr:table->item(row,0);
            if(item) userAction(item->data(Qt::UserRole+1).toString()=="approved"?"revoked":"approved");
        });
        disconnectButton=new QPushButton("Desconectar"); disconnectButton->setObjectName("disconnectButton"); actions->addWidget(disconnectButton); connect(disconnectButton,&QPushButton::clicked,this,[this]{userAction("disconnect");});
        deleteButton=new QPushButton("Eliminar cuenta"); deleteButton->setObjectName("deleteButton"); deleteButton->setProperty("danger",true); actions->addWidget(deleteButton); connect(deleteButton,&QPushButton::clicked,this,[this]{userAction("delete");});
        actions->addStretch(); usersLayout->addLayout(actions);
        connect(table,&QTableWidget::itemSelectionChanged,this,[this]{updateButtons();}); tabs->addTab(users,"Usuarios");
        auto settings=new QWidget; auto form=new QFormLayout(settings); host=new QLineEdit; port=new QSpinBox; udp=new QSpinBox; port->setRange(1,65535); udp->setRange(1,65535); allow=new QLineEdit; autoApprove=new QCheckBox("Aprobar automáticamente hasta diez cuentas");
        auto cfg=chat::readJson(directory+"/settings.json"); host->setText(cfg["host"].toString("0.0.0.0")); port->setValue(cfg["port"].toInt(8765)); udp->setValue(cfg["discovery_port"].toInt(8764)); allow->setText(cfg["allow"].toString("auto")); autoApprove->setChecked(cfg["auto_approve"].toBool(true));
        form->addRow("Dirección de escucha",host); form->addRow("Puerto TCP/TLS",port); form->addRow("Descubrimiento UDP",udp); form->addRow("Subredes permitidas (auto o CIDR)",allow); form->addRow(autoApprove);
        auto info=new QLabel("Los cambios se aplican al guardar. Si el servidor está encendido, se reinicia y los clientes se reconectan.\nSi cambias los puertos, también debes actualizar los clientes."); info->setWordWrap(true); form->addRow(info);
        saveSettingsButton=new QPushButton("Guardar y aplicar"); saveSettingsButton->setObjectName("saveSettingsButton"); form->addRow(saveSettingsButton); connect(saveSettingsButton,&QPushButton::clicked,this,[this] {
            try {
                if(QHostAddress(host->text()).isNull()||host->text().contains(':')) throw std::runtime_error("Introduce una dirección IPv4 válida");
                if(allow->text()!="auto") for(auto cidr:allow->text().split(',')) if(QHostAddress::parseSubnet(cidr.trimmed()).second<0) throw std::runtime_error("Subred inválida");
                chat::writeJson(directory+"/settings.json",{{"host",host->text()},{"port",port->value()},{"discovery_port",udp->value()},{"allow",allow->text()},{"auto_approve",autoApprove->isChecked()}});
                if(serviceState=="active") action("restart","Configuración aplicada; servidor reiniciado.");
                else feedback->setText("Configuración guardada. Se aplicará al encender el servidor.");
            } catch(const std::exception &e) { QMessageBox::warning(this,"Configuración",e.what()); }
        });
        auto backup=new QPushButton("Guardar copia de seguridad"); form->addRow(backup); connect(backup,&QPushButton::clicked,this,[this] {
            auto destination=QFileDialog::getSaveFileName(this,"Nueva copia SQLite",QDir::homePath()+(goBackend?"/chat-lan-go-backup.sqlite3":"/chat-lan-cpp-backup.sqlite3"),"SQLite (*.sqlite3)"); if(destination.isEmpty()) return;
            try { store.backup(destination); feedback->setText("Base copiada. Conserva también server.key/server.pem por separado como backup privado."); } catch(const std::exception &e) { QMessageBox::warning(this,"Copia",e.what()); }
        }); tabs->addTab(settings,"Configuración");
        logs=new QTextEdit; logs->setReadOnly(true); tabs->addTab(logs,"Registros"); refreshTimer.setInterval(2000); connect(&refreshTimer,&QTimer::timeout,this,[this]{refresh();}); refreshTimer.start(); refresh();
    }
};
}
int main(int argc,char **argv) {
    QApplication app(argc,argv); app.setApplicationName("ChatLANCppAdmin"); app.setDesktopFileName("chat-lan-cpp-admin"); app.setStyleSheet(style);
    QCommandLineParser p; p.addHelpOption(); p.addOptions({{"data","Carpeta del servidor","ruta"},{"unit","Unidad systemd de usuario","unidad"},{"go","Administrar el servidor Go"}}); p.process(app);
    auto goBackend=p.isSet("go");
    auto directory=p.isSet("data")?p.value("data"):(goBackend?qEnvironmentVariable("XDG_DATA_HOME",QDir::homePath()+"/.local/share")+"/chat-lan-go/server":chat::defaultServerDir());
    auto unit=p.isSet("unit")?p.value("unit"):(goBackend?"chat-lan-go":"chat-lan-server-cpp");
    try { Admin window(directory,unit,goBackend); window.show(); return app.exec(); } catch(const std::exception &e) { QMessageBox::critical(nullptr,"Administrador",e.what()); return 1; }
}

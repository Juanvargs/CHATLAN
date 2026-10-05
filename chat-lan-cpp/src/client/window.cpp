#include "window.h"
#include "common/paths.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QSplitter>
#include <QCloseEvent>
#include <QMenu>
#include <QApplication>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QScrollBar>
#include <QUuid>
#include <QFrame>
#include <QHBoxLayout>
#include <algorithm>

namespace chat {
QString theme() { return R"(
QWidget{font-family:sans-serif;font-size:14px;color:#e9edef;}
QMainWindow,QDialog{background:#111b21;}
QLabel{background:transparent;}
QLineEdit,QSpinBox,QComboBox{background:#202c33;color:#e9edef;border:0;border-radius:9px;padding:9px;selection-background-color:#006d5b;}
QListWidget,QTableWidget,QTextEdit{background:#111b21;color:#e9edef;border:0;outline:none;}
QListWidget::item{border-bottom:1px solid #202c33;}
QListWidget::item:selected,QTableWidget::item:selected{background:#2a3942;color:#e9edef;}
QListWidget::item:hover{background:#202c33;}
QPushButton{background:#00a884;color:#071b17;border:0;border-radius:9px;padding:10px 15px;font-weight:600;}
QPushButton:hover{background:#06cf9c;} QPushButton:disabled{background:#2a3942;color:#8696a0;}
QPushButton[danger="true"]{background:#80505e;} QMenu{background:#202c33;} QMenu::item:selected{background:#2a3942;}
QScrollBar:vertical{background:#111b21;width:9px;} QScrollBar::handle:vertical{background:#374c57;border-radius:4px;min-height:24px;}
QSplitter::handle{background:#2a3942;width:1px;}
)"; }
static QString initials(const QString &label) {
    QString result;
    for(const auto &part: label.trimmed().split(' ',Qt::SkipEmptyParts)) {
        result += part.left(1).toUpper();
        if(result.size()==2) break;
    }
    return result.isEmpty()?"?":result;
}
static QString avatarColor(const QString &label) {
    static const QStringList colors{"#186a63","#704c88","#725334","#395e83","#774b5e","#526638"};
    return colors.at(qHash(label)%colors.size());
}
static void styleAvatar(QLabel *avatar,const QString &label,int size,bool general=false) {
    avatar->setText(general?"✦":initials(label));
    avatar->setAlignment(Qt::AlignCenter);
    avatar->setFixedSize(size,size);
    avatar->setStyleSheet(QString("QLabel{background:%1;color:#f5faf8;border-radius:%2px;font-size:%3px;font-weight:700;}")
        .arg(general?"#006d5b":avatarColor(label)).arg(size/2).arg(size/3));
}
QString chooseProfile(QWidget *parent) {
    QStringList available;
    if(QDir(dataRoot()+"/client").exists()) available.append(dataRoot()+"/client");
    auto profiles=QDir(dataRoot()+"/profiles");
    for(const auto &folder:profiles.entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name)) available.append(profiles.filePath(folder));
    if(available.size()==1) return available.first();
    QDialog dialog(parent); dialog.setWindowTitle("Elegir usuario · Chat LAN"); dialog.resize(450,370);
    auto layout = new QVBoxLayout(&dialog); auto title = new QLabel("¿Quién va a usar Chat LAN?");
    title->setStyleSheet("font-size:22px;font-weight:600;padding:12px 0"); layout->addWidget(title);
    auto subtitle = new QLabel("Cada perfil conserva su identidad y configuración."); subtitle->setWordWrap(true); layout->addWidget(subtitle);
    auto list = new QListWidget; layout->addWidget(list);
    auto refresh = [&] {
        list->clear(); QStringList paths;
        if(QDir(dataRoot()+"/client").exists()) paths.append(dataRoot()+"/client");
        auto folder=QDir(dataRoot()+"/profiles");
        for(auto path:folder.entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name)) paths.append(folder.filePath(path));
        for(auto path:paths) {
            QString label=path==dataRoot()+"/client"?"Usuario principal":QFileInfo(path).fileName();
            try {
                auto cfg=readJson(path+"/connection.json"); label=cfg["name"].toString(label);
                if(QFile::exists(path+"/profile-name") && cfg["name"].toString().isEmpty()) label=QString::fromUtf8(readFile(path+"/profile-name")).trimmed();
                auto code=cfg["client"].toObject()["code"].toInt(); if(code) label+=QString(" · %1").arg(code,3,10,QChar('0'));
            } catch(...) {}
            auto item=new QListWidgetItem(label,list); item->setData(Qt::UserRole,path);
        }
        if(list->count()) list->setCurrentRow(0);
    };
    auto row = new QHBoxLayout; auto create = new QPushButton("＋ Nuevo usuario"); row->addWidget(create); row->addStretch();
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Open|QDialogButtonBox::Cancel); buttons->button(QDialogButtonBox::Open)->setText("Abrir"); row->addWidget(buttons); layout->addLayout(row);
    QObject::connect(create,&QPushButton::clicked,&dialog,[&] {
        bool ok=false; auto name=QInputDialog::getText(&dialog,"Nuevo usuario","Nombre (1–32 caracteres):",QLineEdit::Normal,{},&ok).trimmed();
        if(!ok || name.isEmpty()) return;
        if(name.size()>32) { QMessageBox::warning(&dialog,"Nombre","Usa hasta 32 caracteres."); return; }
        try {
            auto path=dataRoot()+"/profiles/"+QUuid::createUuid().toString(QUuid::WithoutBraces); privateDir(path); writeFile(path+"/profile-name",name.toUtf8());
            refresh(); for(int i=0;i<list->count();++i) if(list->item(i)->data(Qt::UserRole)==path) list->setCurrentRow(i);
        } catch(const std::exception &e) { QMessageBox::warning(&dialog,"Perfil",e.what()); }
    });
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,[&] { if(list->currentItem()) dialog.accept(); else QMessageBox::information(&dialog,"Perfil","Crea primero un usuario."); });
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(list,&QListWidget::itemDoubleClicked,&dialog,[&](QListWidgetItem*) { dialog.accept(); });
    refresh(); if(dialog.exec()!=QDialog::Accepted) return {}; return list->currentItem()->data(Qt::UserRole).toString();
}
MessageView::MessageView(QWidget *parent) : QScrollArea(parent) {
    setObjectName("messageView"); setWidgetResizable(true); setFrameShape(QFrame::NoFrame);
    setStyleSheet("QScrollArea,QScrollArea>QWidget>QWidget{background:#0b141a;border:0;}");
    body=new QWidget; body->setStyleSheet("background:#0b141a;"); column=new QVBoxLayout(body); column->setContentsMargins(32,24,32,24); column->setSpacing(7); setWidget(body);
    column->addStretch(); renderTimer.setSingleShot(true); connect(&renderTimer,&QTimer::timeout,this,&MessageView::rebuild);
}
void MessageView::setMessages(const QList<QJsonObject> &items,const QString &id) { pending=items; myId=id; renderTimer.start(0); }
void MessageView::rebuild() {
    while(auto item=column->takeAt(0)) { if(item->widget()) delete item->widget(); delete item; }
    for(auto packet:pending) {
        auto sender=packet["sender"].toObject(); bool mine=sender["id"].toString()==myId;
        auto wrapper=new QWidget; auto row=new QHBoxLayout(wrapper); row->setContentsMargins(0,0,0,0); row->setSpacing(0);
        auto bubble=new QLabel; bubble->setObjectName("messageBubble"); bubble->setWordWrap(true); bubble->setTextFormat(Qt::RichText); bubble->setTextInteractionFlags(Qt::TextSelectableByMouse); bubble->setMaximumWidth(560);
        auto time=QDateTime::fromString(packet["time"].toString(),Qt::ISODateWithMs); if(!time.isValid()) time=QDateTime::fromString(packet["time"].toString(),Qt::ISODate);
        auto content=packet["text"].toString().toHtmlEscaped().replace('\n',"<br>");
        bubble->setText(QString("<b>%1 · %2</b><br>%3<br><span style='font-size:11px;color:#b7c8dc'>%4</span>").arg(sender["name"].toString().toHtmlEscaped()).arg(sender["code"].toInt(),3,10,QChar('0')).arg(content,time.isValid()?time.toLocalTime().toString("HH:mm"):"--:--"));
        bubble->setStyleSheet(mine?"QLabel{background:#005c4b;color:#e9edef;border:0;border-radius:9px;padding:8px 11px;}":"QLabel{background:#202c33;color:#e9edef;border:0;border-radius:9px;padding:8px 11px;}");
        if(mine) row->addStretch(); row->addWidget(bubble); if(!mine) row->addStretch(); column->addWidget(wrapper);
    }
    column->addStretch(); scrollToBottom();
}
void MessageView::scrollToBottom() {
    column->activate(); body->adjustSize();
    QTimer::singleShot(0,this,[this] { verticalScrollBar()->setValue(verticalScrollBar()->maximum()); });
    QTimer::singleShot(100,this,[this] { verticalScrollBar()->setValue(verticalScrollBar()->maximum()); });
}
QString MessageView::plainText() const { QString result; for(auto packet:pending) result+=packet["text"].toString()+"\n"; return result; }
Window::Window(const QString &path,QWidget *parent) : QMainWindow(parent), profile(path), identity(path), connection(identity,this) {
    privateDir(profile); setWindowTitle("Chat LAN"); setWindowIcon(QIcon(":/logo.png")); resize(1180,760);
    config=readJson(profile+"/connection.json");
    if(config.isEmpty()) {
        QString defaults=dataRoot()+"/client-default.json";
        if(!QFile::exists(defaults)) {
            defaults=QCoreApplication::applicationDirPath()+"/client-default.json";
            if(!QFile::exists(defaults)) defaults=QDir(QCoreApplication::applicationDirPath()).filePath("../share/chat-lan-cpp/client-default.json");
        }
        if(QFile::exists(defaults)) config=readJson(defaults);
        if(config.isEmpty() && QFile::exists(":/client-default.json")) { defaults=":/client-default.json"; config=readJson(defaults); }
        if(config.isEmpty()) config={{"host","127.0.0.1"},{"port",8765},{"discovery_port",8764}};
        if(config["certificate"].toString().isEmpty() && QFile::exists(defaultServerDir()+"/server.pem")) config["certificate"]=defaultServerDir()+"/server.pem";
        auto certificate=config["certificate"].toString();
        if(!certificate.isEmpty() && !certificate.startsWith(":") && QFileInfo(certificate).isRelative()) certificate=QFileInfo(defaults).absoluteDir().filePath(certificate);
        if(!certificate.isEmpty() && QFile::exists(certificate)) { writeFile(profile+"/server.pem",readFile(certificate),false); config["certificate"]=profile+"/server.pem"; }
        if(QFile::exists(profile+"/profile-name")) config["name"]=QString::fromUtf8(readFile(profile+"/profile-name")).trimmed();
        saveConfig();
    }
    auto root=new QWidget; setCentralWidget(root);
    auto layout=new QHBoxLayout(root); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    auto rail=new QWidget; rail->setFixedWidth(62); rail->setStyleSheet("background:#202c33;border-right:1px solid #2a3942;");
    auto railLayout=new QVBoxLayout(rail); railLayout->setContentsMargins(8,20,8,14); railLayout->setSpacing(12);
    auto railBrand=new QLabel; railBrand->setPixmap(QPixmap(":/logo.png").scaled(42,42,Qt::KeepAspectRatio,Qt::SmoothTransformation)); railBrand->setAlignment(Qt::AlignCenter); railBrand->setToolTip("Chat LAN"); railLayout->addWidget(railBrand);
    railLayout->addStretch();
    selfAvatar=new QLabel; styleAvatar(selfAvatar,config["name"].toString(),42); selfAvatar->setToolTip("Tu perfil"); railLayout->addWidget(selfAvatar,0,Qt::AlignHCenter); layout->addWidget(rail);
    auto splitter=new QSplitter(Qt::Horizontal); splitter->setChildrenCollapsible(false); layout->addWidget(splitter,1);

    auto sidebar=new QWidget; sidebar->setMinimumWidth(295); sidebar->setMaximumWidth(480); sidebar->setStyleSheet("background:#111b21;");
    auto sideLayout=new QVBoxLayout(sidebar); sideLayout->setContentsMargins(0,0,0,0); sideLayout->setSpacing(0);
    auto sideHeader=new QWidget; sideHeader->setStyleSheet("background:#202c33;"); auto sideHeaderLayout=new QHBoxLayout(sideHeader); sideHeaderLayout->setContentsMargins(20,13,18,13);
    auto title=new QLabel("Chats"); title->setStyleSheet("font-size:22px;font-weight:700;"); sideHeaderLayout->addWidget(title); sideHeaderLayout->addStretch();
    idLabel=new QLabel("ID pendiente"); idLabel->setObjectName("clientCode"); idLabel->setStyleSheet("color:#aebac1;font-size:12px;"); sideHeaderLayout->addWidget(idLabel); sideLayout->addWidget(sideHeader);
    search=new QLineEdit; search->setObjectName("conversationSearch"); search->setPlaceholderText("⌕  Buscar conversación");
    auto searchWrap=new QWidget; auto searchLayout=new QHBoxLayout(searchWrap); searchLayout->setContentsMargins(14,12,14,12); searchLayout->addWidget(search); sideLayout->addWidget(searchWrap);
    targets=new QListWidget; targets->setObjectName("conversations"); targets->setMinimumWidth(220); sideLayout->addWidget(targets,1);
    auto account=new QWidget; account->setStyleSheet("background:#202c33;"); auto accountLayout=new QVBoxLayout(account); accountLayout->setContentsMargins(12,7,12,6); accountLayout->setSpacing(2);
    auto identityRow=new QHBoxLayout; name=new QLineEdit(config["name"].toString()); name->setObjectName("userName"); name->setMaxLength(32); name->setPlaceholderText("Tu nombre"); name->setReadOnly(!name->text().isEmpty()); identityRow->addWidget(name,1);
    connectButton=new QPushButton("Conectar"); connectButton->setObjectName("connectButton"); connectButton->setStyleSheet("QPushButton{color:white;} QPushButton:hover{color:white;} QPushButton:disabled{color:white;}"); identityRow->addWidget(connectButton); accountLayout->addLayout(identityRow);
    name->setFixedHeight(36); connectButton->setFixedHeight(36);
    state=new QLabel("Elige tu usuario y pulsa Conectar."); state->setObjectName("connectionStatus"); state->setWordWrap(true); state->setMaximumHeight(34); state->setStyleSheet("color:#aebac1;font-size:11px;"); accountLayout->addWidget(state); sideLayout->addWidget(account); splitter->addWidget(sidebar);

    auto chat=new QWidget; chat->setStyleSheet("background:#0b141a;"); auto chatLayout=new QVBoxLayout(chat); chatLayout->setContentsMargins(0,0,0,0); chatLayout->setSpacing(0);
    auto chatHeader=new QWidget; chatHeader->setFixedHeight(66); chatHeader->setStyleSheet("background:#202c33;"); auto chatHeaderLayout=new QHBoxLayout(chatHeader); chatHeaderLayout->setContentsMargins(18,8,18,8); chatHeaderLayout->setSpacing(12);
    chatAvatar=new QLabel; styleAvatar(chatAvatar,"Sala general",42,true); chatHeaderLayout->addWidget(chatAvatar);
    auto heading=new QVBoxLayout; heading->setSpacing(2); chatTitle=new QLabel("Sala general"); chatTitle->setStyleSheet("font-size:16px;font-weight:600;"); heading->addWidget(chatTitle);
    chatSubtitle=new QLabel("Conversación de la LAN"); chatSubtitle->setStyleSheet("color:#aebac1;font-size:12px;"); heading->addWidget(chatSubtitle); chatHeaderLayout->addLayout(heading); chatHeaderLayout->addStretch(); chatLayout->addWidget(chatHeader);
    messages=new MessageView; chatLayout->addWidget(messages,1);
    auto composeWrap=new QWidget; composeWrap->setStyleSheet("background:#202c33;"); auto compose=new QHBoxLayout(composeWrap); compose->setContentsMargins(18,10,18,10); compose->setSpacing(10);
    text=new QLineEdit; text->setObjectName("messageInput"); text->setMaxLength(2000); text->setPlaceholderText("Escribe un mensaje"); compose->addWidget(text,1);
    sendButton=new QPushButton("Enviar ➤"); sendButton->setObjectName("sendButton"); compose->addWidget(sendButton); chatLayout->addWidget(composeWrap);
    splitter->addWidget(chat); splitter->setSizes({350,830}); splitter->setStretchFactor(1,1);
    tray=new QSystemTrayIcon(QIcon(":/logo.png"),this); tray->setToolTip("Chat LAN");
    auto menu=new QMenu(this); menu->addAction("Mostrar Chat LAN",this,&QWidget::showNormal); menu->addAction("Salir",this,&QWidget::close); tray->setContextMenu(menu); if(QSystemTrayIcon::isSystemTrayAvailable()) tray->show();
    connect(connectButton,&QPushButton::clicked,this,[this] { if(connection.isRunning() || connection.isAuthenticated()) { connection.stop(); state->setText("Desconectado. Identidad conservada."); } else connectChat(); });
    connect(sendButton,&QPushButton::clicked,this,&Window::sendMessage); connect(text,&QLineEdit::returnPressed,this,&Window::sendMessage);
    connect(targets,&QListWidget::currentItemChanged,this,[this] { markConversationRead(); renderHistory(); });
    connect(search,&QLineEdit::textChanged,this,[this] { filterTargets(); });
    connect(&connection,&Connection::status,state,&QLabel::setText);
    connect(&connection,&Connection::online,this,[this](bool online) { connectButton->setText(online?"Desconectar":connection.isRunning()?"Cancelar conexión":"Conectar"); sendButton->setEnabled(online); text->setEnabled(online); });
    connect(&connection,&Connection::message,this,&Window::onMessage);
    connect(&connection,&Connection::serverFound,this,[this](const QString &host,int port) { config["host"]=host; config["port"]=port; try { saveConfig(); } catch(const std::exception &e) { state->setText(e.what()); } });
    auto client=config["client"].toObject(); myId=client["id"].toString(); if(!myId.isEmpty()) { idLabel->setText(QString("ID %1").arg(client["code"].toInt(),3,10,QChar('0'))); idLabel->setToolTip(myId); }
    rebuildTargets(); sendButton->setEnabled(false); text->setEnabled(false);
}
void Window::saveConfig() { writeJson(profile+"/connection.json",config); }
void Window::connectChat() {
    try {
        if(name->text().trimmed().isEmpty()) throw std::runtime_error("Escribe tu nombre");
        auto certificate=config["certificate"].toString();
        if(certificate.isEmpty() || !QFile::exists(certificate)) {
            certificate=QFileDialog::getOpenFileName(this,"Certificado público del servidor",QDir::homePath(),"Certificado (*.pem)"); if(certificate.isEmpty()) return;
            bool ok=false; auto host=QInputDialog::getText(this,"Servidor LAN","IP inicial del servidor:",QLineEdit::Normal,config["host"].toString("127.0.0.1"),&ok); if(!ok || host.trimmed().isEmpty()) return;
            writeFile(profile+"/server.pem",readFile(certificate),false); config["certificate"]=profile+"/server.pem"; config["host"]=host.trimmed();
            auto seed=config; seed.remove("name"); seed.remove("client"); seed["certificate"]=certificate; writeJson(dataRoot()+"/client-default.json",seed);
        }
        config["name"]=name->text().trimmed(); saveConfig();
        auto port=config["port"].toInt(8765), udp=config["discovery_port"].toInt(8764); if(port<1||port>65535||udp<1||udp>65535) throw std::runtime_error("Puerto inválido");
        connection.start(config["host"].toString("127.0.0.1"),port,config["certificate"].toString(),config["name"].toString(),udp); connectButton->setText("Cancelar conexión");
    } catch(const std::exception &e) { state->setText(e.what()); QMessageBox::warning(this,"Conexión",e.what()); }
}
QString Window::target() const { auto item=targets->currentItem(); return item?item->data(Qt::UserRole).toString():"general"; }
void Window::rebuildTargets() {
    auto selected=target(); targets->blockSignals(true); targets->clear();
    auto addConversation=[this](const QString &id,const QString &label,bool general) {
        auto item=new QListWidgetItem(label,targets); item->setData(Qt::UserRole,id); item->setSizeHint(QSize(0,74));
        auto row=new QWidget; auto line=new QHBoxLayout(row); line->setContentsMargins(14,8,14,8); line->setSpacing(12);
        auto avatar=new QLabel; styleAvatar(avatar,label,44,general); line->addWidget(avatar);
        auto details=new QVBoxLayout; details->setSpacing(3); auto nameLabel=new QLabel(label); nameLabel->setStyleSheet("font-size:14px;font-weight:600;"); details->addWidget(nameLabel);
        QString preview=general?"Mensajes para todos":onlineUsers.contains(id)?"En línea":"Desconectado";
        if(!histories.value(id).isEmpty()) {
            const auto last=histories.value(id).last(); preview=(last["sender"].toObject()["id"].toString()==myId?"Tú: ":"")+last["text"].toString().simplified();
            if(preview.size()>42) preview=preview.left(41)+"…";
        }
        auto previewLabel=new QLabel(preview); previewLabel->setStyleSheet("color:#8696a0;font-size:12px;"); details->addWidget(previewLabel); line->addLayout(details,1);
        auto side=new QVBoxLayout; side->setAlignment(Qt::AlignTop|Qt::AlignRight);
        if(!histories.value(id).isEmpty()) {
            auto time=QDateTime::fromString(histories.value(id).last()["time"].toString(),Qt::ISODateWithMs);
            if(!time.isValid()) time=QDateTime::fromString(histories.value(id).last()["time"].toString(),Qt::ISODate);
            if(time.isValid()) { auto stamp=new QLabel(time.toLocalTime().toString("HH:mm")); stamp->setStyleSheet("color:#8696a0;font-size:11px;"); side->addWidget(stamp); }
        }
        const auto unread=unreadCounts.value(id);
        if(unread>0) {
            auto badge=new QLabel(unread>99?"99+":QString::number(unread)); badge->setObjectName("unreadBadge");
            badge->setAlignment(Qt::AlignCenter); badge->setMinimumSize(22,22);
            badge->setStyleSheet("QLabel{background:#00a884;color:#071b17;border-radius:11px;font-size:11px;font-weight:700;padding:0 5px;}");
            side->addWidget(badge,0,Qt::AlignRight);
        }
        line->addLayout(side); targets->setItemWidget(item,row);
        if(!general && !onlineUsers.contains(id)) item->setToolTip("Desconectado: puedes ver historial, pero no enviar un privado.");
    };
    addConversation("general","Sala general",true);
    QList<QJsonObject> users=knownUsers.values();
    std::sort(users.begin(),users.end(),[](const QJsonObject &a,const QJsonObject &b) { return a["name"].toString().localeAwareCompare(b["name"].toString())<0; });
    for(const auto &user:users) {
        auto id=user["id"].toString(); if(id==myId) continue;
        addConversation(id,QString("%1 · %2").arg(user["name"].toString()).arg(user["code"].toInt(),3,10,QChar('0')),false);
    }
    targets->setCurrentRow(0); for(int i=0;i<targets->count();++i) if(targets->item(i)->data(Qt::UserRole)==selected) targets->setCurrentRow(i);
    targets->blockSignals(false); filterTargets(); renderHistory();
}
void Window::filterTargets() {
    const auto query=search->text().trimmed();
    for(int i=0;i<targets->count();++i) {
        auto item=targets->item(i);
        item->setHidden(!query.isEmpty() && !item->text().contains(query,Qt::CaseInsensitive));
    }
}
void Window::markConversationRead() {
    const auto id=target();
    if(unreadCounts.remove(id)==0) return;
    if(auto item=targets->currentItem()) if(auto row=targets->itemWidget(item))
        if(auto badge=row->findChild<QLabel*>("unreadBadge")) badge->hide();
}
void Window::renderHistory() {
    auto id=target(); const bool general=id=="general"; auto label=general?"Sala general":targets->currentItem()?targets->currentItem()->text():"Sala general";
    chatTitle->setText(label); styleAvatar(chatAvatar,label,42,general);
    chatSubtitle->setText(general?QString("%1 participante(s) en línea").arg(onlineUsers.size()):onlineUsers.contains(id)?"En línea":"Desconectado");
    messages->setMessages(histories.value(id),myId);
}
void Window::onMessage(const QJsonObject &packet) {
    auto kind=packet["type"].toString();
    if(kind=="authenticated" || kind=="registration_pending") {
        QJsonObject user{{"id",packet["id"]},{"name",packet["name"]},{"code",packet["code"]}};
        myId=user["id"].toString(); config["client"]=user; config["name"]=user["name"]; name->setText(user["name"].toString()); name->setReadOnly(true);
        styleAvatar(selfAvatar,user["name"].toString(),42);
        idLabel->setText(QString("ID %1").arg(user["code"].toInt(),3,10,QChar('0'))); idLabel->setToolTip(myId);
        histories.clear(); knownUsers.clear(); unreadCounts.clear(); seen.clear(); seenOrder.clear();
        try { saveConfig(); } catch(const std::exception &e) { state->setText(e.what()); } rebuildTargets();
    } else if(kind=="presence") {
        onlineUsers.clear(); for(auto item:packet["users"].toArray()) { auto user=item.toObject(); knownUsers[user["id"].toString()]=user; onlineUsers.insert(user["id"].toString()); } rebuildTargets();
    } else if(kind=="history_reset") { histories.clear(); knownUsers.clear(); unreadCounts.clear(); seen.clear(); seenOrder.clear(); rebuildTargets();
    } else if(kind=="account_deleted") {
        connection.stop(); histories.clear(); knownUsers.clear(); unreadCounts.clear(); messages->setMessages({},myId);
        QStringList failures; for(auto file:QStringList{"identity.key","connection.json","server.pem","profile-name"}) { auto path=profile+"/"+file; if(QFile::exists(path) && !QFile::remove(path)) failures.append(file); }
        state->setText(failures.isEmpty()?"Cuenta eliminada y archivos de perfil borrados.":"Cuenta eliminada; no se pudieron borrar: "+failures.join(", "));
        connectButton->setEnabled(false); name->setEnabled(false);
        tray->showMessage("Chat LAN",state->text(),QSystemTrayIcon::Warning,5000);
        if(failures.isEmpty()) { QDir().rmdir(profile); QTimer::singleShot(1000,this,&QWidget::close); }
    } else if(kind=="message") {
        auto sender=packet["sender"].toObject(); auto senderId=sender["id"].toString(); auto key=senderId+":"+packet["message_id"].toString(); if(seen.contains(key)) return;
        seen.insert(key); seenOrder.append(key); if(seenOrder.size()>2000) seen.remove(seenOrder.takeFirst()); knownUsers[senderId]=sender;
        auto destination=packet["to"].toString(); auto id=destination=="general"?"general":senderId==myId?destination:senderId;
        auto &history=histories[id]; history.append(packet); if(history.size()>200) history.removeFirst();
        if(!packet["history"].toBool() && senderId!=myId && id!=target()) unreadCounts[id]=qMin(999,unreadCounts.value(id)+1);
        rebuildTargets();
        if(!packet["history"].toBool() && senderId!=myId) {
            auto notice=QString("%1 · %2 envió un mensaje").arg(sender["name"].toString()).arg(sender["code"].toInt(),3,10,QChar('0'));
            tray->showMessage("Chat LAN",notice,QSystemTrayIcon::Information,5000);
        }
    } else if(kind=="error") state->setText(packet["text"].toString());
    else if(kind=="accepted") state->setText("Mensaje guardado y aceptado por el servidor.");
    else if(kind=="delivered") state->setText("Mensaje recibido por una aplicación destinataria.");
}
void Window::sendMessage() {
    auto content=text->text().trimmed(); if(content.isEmpty()) return;
    try { connection.send(chat::event("send_message",{{"message_id",QUuid::createUuid().toString(QUuid::WithoutBraces)},{"to",target()},{"text",content}})); text->clear(); } catch(const std::exception &e) { state->setText(e.what()); }
}
void Window::closeEvent(QCloseEvent *event) { connection.stop(); tray->hide(); event->accept(); }
}

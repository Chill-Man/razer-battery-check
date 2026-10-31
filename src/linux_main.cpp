#include "battery.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QKeyEvent>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVariantAnimation>
#include <atomic>
#include <condition_variable>
#include <cmath>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>
using namespace pulse;
namespace {
std::filesystem::path DataPath(const wchar_t* file) {
    auto dir=QString::fromStdWString(DataDirectory());return std::filesystem::u8path((dir+"/"+QString::fromWCharArray(file)).toStdString());
}
QImage TightImage(const QImage& input) {
    auto image=input.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    int left=image.width(),top=image.height(),right=-1,bottom=-1;
    for(int y=0;y<image.height();++y){auto row=reinterpret_cast<const QRgb*>(image.constScanLine(y));for(int x=0;x<image.width();++x)if(qAlpha(row[x])>8){left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}}
    return right>=left?image.copy(QRect(QPoint(left,top),QPoint(right,bottom))):QImage();
}
QString ProgramPath(){return qEnvironmentVariableIsSet("APPIMAGE")?qEnvironmentVariable("APPIMAGE"):QCoreApplication::applicationFilePath();}
QString AutostartPath(){return QDir(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)).filePath("autostart/basilisk-pulse.desktop");}
bool Autostart(){return QFile::exists(AutostartPath());}
bool SetAutostart(bool enabled){
    if(!enabled)return !Autostart()||QFile::remove(AutostartPath());
    QString path=ProgramPath();path.replace("\\","\\\\").replace("\"","\\\"").replace("`","\\`").replace("$","\\$").replace("%","%%");
    QDir().mkpath(QFileInfo(AutostartPath()).absolutePath());QFile file(AutostartPath());
    if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;
    return file.write(("[Desktop Entry]\nType=Application\nName=Basilisk Pulse\nExec=\""+path+"\"\nIcon=basilisk-pulse\nTerminal=false\nX-GNOME-Autostart-enabled=true\n").toUtf8())>0;
}
enum Action{Refresh=1,Settings,About,Exit,Startup,Motion,Economy,Minus,Plus,Back,FastPreset,MiddlePreset,DefaultPreset};
struct Hit{QRectF bounds;int action;};
class Panel:public QWidget {
public:
    TelemetryState state;Estimate estimate;QSystemTrayIcon tray;QMenu trayMenu;
    QSettings preferences;
    std::function<void()> refresh;
    std::atomic<unsigned> interval{120};
    bool settingsPage=false,inspect=false,preview=false;
    double nominal=110;
    Panel(bool inspectMode,bool previewMode):QWidget(nullptr,QSystemTrayIcon::isSystemTrayAvailable()?Qt::Tool|Qt::FramelessWindowHint:Qt::Window|Qt::FramelessWindowHint),preferences(QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("settings.ini"),QSettings::IniFormat){
        inspect=inspectMode;preview=previewMode;setWindowTitle("Basilisk Pulse");setFixedSize(320,344);setAttribute(Qt::WA_TranslucentBackground);setMouseTracking(true);setWindowIcon(QIcon(":/icon.png"));
        animations=preferences.value("animations",true).toBool();interval=preferences.value("economy",false).toBool()?300:120;
        auto cached=LoadLastReading(DataPath(L"last-reading.tsv"));
        if(cached.percent<0)for(uint16_t pid:{uint16_t(0xAB),uint16_t(0xCD)}){
            auto model=ModelForPid(pid);History h;h.Load(DataPath(model->historyFile));
            if(!h.samples.empty()&&h.samples.back().time>cached.time){auto sample=h.samples.back();cached.pid=pid;cached.raw=sample.raw;cached.percent=static_cast<int>(std::lround(sample.raw*100.0/255));cached.charging=sample.charging;cached.time=sample.time;}
        }
        if(preview){cached.pid=0xAB;cached.raw=204;cached.percent=80;cached.charging=0;cached.time=Now();}
        state.Restore(cached);loadProfile();History h;h.Load(DataPath(model().historyFile));estimate=h.Predict(cached.percent,nominal);
        photo=TightImage(QImage(":/mouse.png"));
        switchPositions[0]=Autostart();switchPositions[1]=animations;switchPositions[2]=interval==300;
        for(int i=0;i<3;++i){switchAnimations[i]=new QVariantAnimation(this);switchAnimations[i]->setDuration(240);switchAnimations[i]->setEasingCurve(QEasingCurve::OutQuart);connect(switchAnimations[i],&QVariantAnimation::valueChanged,this,[this,i](const QVariant& value){switchPositions[i]=value.toFloat();update();});}
        ambient.setInterval(33);connect(&ambient,&QTimer::timeout,this,[this]{update(QRect(210,44,104,137));});clock.start();
        statusAction=trayMenu.addAction("Waiting for a reading");statusAction->setEnabled(false);runtimeAction=trayMenu.addAction("—");runtimeAction->setEnabled(false);trayMenu.addSeparator();
        trayMenu.addAction("Open panel",this,[this]{openPanel();});trayMenu.addAction("Refresh",this,[this]{request();});trayMenu.addAction("Exit",qApp,&QApplication::quit);
        tray.setContextMenu(&trayMenu);connect(&tray,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){if(reason==QSystemTrayIcon::Trigger||reason==QSystemTrayIcon::DoubleClick)openPanel();});
        if(!preview)tray.show();updateTray();
    }
    const ModelInfo& model()const{auto info=ModelForPid(state.reading.pid);return *(info?info:ModelForPid(0xAB));}
    QString profileKey()const{return QString::fromWCharArray(model().nominalSetting);}
    void loadProfile(){nominal=std::clamp(preferences.value(profileKey(),model().nominalHours).toDouble(),5.0,300.0);}
    QString runtime()const{
        const auto& reading=state.reading;if(reading.percent<0)return "Waiting for mouse";
        if(state.LooksLive(Now())&&state.ChargingState(Now())==1)return "Charging";
        if(reading.percent<=1)return "Time to charge";
        double hours=estimate.learned?estimate.hours:std::max(0,reading.percent-1)*nominal/100;
        if(!estimate.learned)return QString::fromUtf8("≈ %1 h").arg(static_cast<int>(std::ceil(hours)));
        int minutes=std::max(5,static_cast<int>(std::round(hours*12))*5);
        return QString::fromUtf8("≈ %1 h%2").arg(minutes/60).arg(minutes%60?QString(" %1 min").arg(minutes%60):QString());
    }
    void acceptReading(const Reading& reading,const Estimate& prediction){
        const auto* oldModel=ModelForPid(state.reading.pid);state.Apply(reading);
        if(reading.connected){if(oldModel!=ModelForPid(reading.pid))loadProfile();estimate=prediction;}
        polling=false;updateTray();syncMotion();update();
        if(reading.connected&&reading.charging==0&&reading.percent<=15&&!warned){tray.showMessage("Time to charge",QString("Basilisk: %1% remaining").arg(reading.percent),QSystemTrayIcon::Information);warned=true;}
        if(reading.connected&&(reading.charging==1||reading.percent>20))warned=false;
    }
    void openPanel(){
        settingsPage=false;QRect work=QGuiApplication::primaryScreen()->availableGeometry();QRect icon=tray.geometry();
        QPoint position=icon.isValid()?QPoint(icon.center().x()-width()/2,icon.top()-height()-12):QPoint(work.right()-width()-12,work.bottom()-height()-12);
        position.setX(std::clamp(position.x(),work.left()+8,std::max(work.left()+8,work.right()-width()-8)));
        position.setY(std::clamp(position.y(),work.top()+8,std::max(work.top()+8,work.bottom()-height()-8)));
        move(position);show();raise();activateWindow();syncMotion();if(Now()-state.reading.time>5)request();
    }
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setPen(Qt::NoPen);p.setBrush(QColor("#161c18"));p.drawRoundedRect(rect(),12,12);hits.clear();
        auto text=[&](const QString& value,QRectF bounds,int size,QColor color=QColor("#eff3ef"),int weight=QFont::Normal,Qt::Alignment align=Qt::AlignLeft){QFont font("DejaVu Sans");font.setPixelSize(size);font.setWeight(weight);p.setFont(font);p.setPen(color);p.drawText(bounds,align|Qt::AlignVCenter,value);p.setPen(Qt::NoPen);};
        auto button=[&](int id,QRectF box,const QString& label,bool accent=false){hits.push_back({box,id});p.setBrush(accent?QColor("#a5f57a"):QColor(hovered==id?"#303632":"#232824"));p.drawRoundedRect(box,10,10);text(label,box,12,accent?QColor("#10230c"):QColor("#dae0dc"),QFont::DemiBold,Qt::AlignHCenter);};
        if(settingsPage){
            button(Back,{16,16,62,32},QString::fromUtf8("‹ Back"));text("Settings",{92,16,208,32},20,QColor("#f1f5f2"),QFont::DemiBold);
            QString labels[]={"Start with Linux","Smooth animations","Economy polling"};
            for(int i=0;i<3;++i){float y=70+i*44;hits.push_back({{12,y,296,40},Startup+i});text(labels[i],{20,y,230,40},13,QColor("#f0f3f1"),QFont::DemiBold);
                float pos=switchPositions[i];QColor off("#3a443d"),on("#a5f57a");p.setBrush(QColor::fromRgbF(off.redF()+(on.redF()-off.redF())*pos,off.greenF()+(on.greenF()-off.greenF())*pos,off.blueF()+(on.blueF()-off.blueF())*pos));p.drawRoundedRect(QRectF(266,y+9,34,22),11,11);p.setBrush(pos>0.5?QColor("#152611"):QColor("#abb4ad"));p.drawEllipse(QPointF(277+12*pos,y+20),8,8);}
            p.setPen(QColor("#303932"));p.drawLine(20,214,300,214);p.setPen(Qt::NoPen);text("Full-charge battery life",{20,225,280,27},14,QColor("#ecf3ed"),QFont::DemiBold);
            button(Minus,{20,258,44,34},QString::fromUtf8("−"));text(QString("%1 hours").arg(static_cast<int>(nominal)),{72,258,176,34},21,QColor("#a5f57a"),QFont::DemiBold,Qt::AlignHCenter);button(Plus,{256,258,44,34},"+");
            button(FastPreset,{20,304,85,30},QString("%1 h").arg(model().fastPollingHours));button(MiddlePreset,{117,304,86,30},"60 h");button(DefaultPreset,{215,304,85,30},QString("%1 h").arg(model().nominalHours));return;
        }
        bool live=state.LooksLive(Now());text(QString::fromWCharArray(model().name),{20,12,280,37},21,QColor("#eff3ef"),QFont::DemiBold);
        QString digits=state.reading.percent>=0?QString::number(state.reading.percent):QString::fromUtf8("—");
        QColor ink=live?QColor("#f5f8f5"):QColor("#88938b");
        if(numberText!=digits||numberColor!=ink||numberRatio!=devicePixelRatioF()){
            numberText=digits;numberColor=ink;numberRatio=devicePixelRatioF();QImage buffer(QSize(200,120)*numberRatio,QImage::Format_ARGB32_Premultiplied);buffer.fill(Qt::transparent);QPainter painter(&buffer);painter.scale(numberRatio,numberRatio);QFont font("DejaVu Sans");font.setPixelSize(84);font.setWeight(QFont::Light);painter.setFont(font);painter.setPen(ink);painter.drawText(QRectF(0,0,200,120),Qt::AlignLeft|Qt::AlignVCenter,digits);painter.end();number=TightImage(buffer);
        }
        float numberHeight=number.height()/numberRatio,numberWidth=number.width()/numberRatio;p.drawImage(QRectF(20,80,numberWidth,numberHeight),number);
        if(state.reading.percent>=0){
            text("%",{numberWidth+30,80+numberHeight-30,30,30},22,QColor("#a0aca3"),QFont::Light);
            float t=ambient.isActive()?clock.elapsed()/1000.0f:0;float breath=0.5f-0.5f*std::cos(t*0.698132f);QPointF center(258+4*std::sin(t*0.31f),80+numberHeight/2-6);
            QRadialGradient gradient(center,48);gradient.setColorAt(0,QColor(216,233,223,static_cast<int>(35+15*breath)));gradient.setColorAt(.5,QColor(186,212,198,12));gradient.setColorAt(1,Qt::transparent);p.setBrush(gradient);p.drawEllipse(center,42,55);
            float mouseWidth=numberHeight*photo.width()/photo.height();p.drawImage(QRectF(261-mouseWidth/2,80,mouseWidth,numberHeight),photo);
        }
        text(!live?"Last known charge":state.ChargingState(Now())==1?"Charging":"Battery level",{20,151,190,26},11,QColor("#98a59c"));
        for(int i=0;i<40;++i){p.setBrush(state.reading.percent>=0&&i<state.reading.percent*.4?live?QColor("#a5f57a"):QColor("#657467"):QColor("#303932"));p.drawRoundedRect(QRectF(20+i*7.05,190,4,12),1.5,1.5);}
        p.setBrush(QColor("#202722"));p.drawRoundedRect(QRectF(12,216,296,50),12,12);text("UNTIL 1% CHARGE",{24,219,272,17},9,QColor("#96a59a"),QFont::DemiBold);text(runtime(),{23,236,274,28},19,QColor("#ecf5e9"),QFont::DemiBold);
        button(Refresh,{20,278,178,32},polling?"Updating…":"Refresh",true);button(Settings,{208,278,92,32},"Settings");hits.push_back({{20,316,150,24},About});hits.push_back({{240,316,60,24},Exit});text(preview?"RENDER PREVIEW":"About",{20,318,170,22},10,QColor("#87958c"));text("Exit",{240,318,60,22},10,QColor("#87958c"),QFont::Normal,Qt::AlignRight);
    }
    void mouseMoveEvent(QMouseEvent* event)override{int hit=0;for(const auto& area:hits)if(area.bounds.contains(event->pos()))hit=area.action;if(hit!=hovered){hovered=hit;update();}setCursor(hit?Qt::PointingHandCursor:Qt::ArrowCursor);}
    void mouseReleaseEvent(QMouseEvent* event)override{if(event->button()!=Qt::LeftButton)return;int action=0;for(const auto& hit:hits)if(hit.bounds.contains(event->pos()))action=hit.action;if(action)perform(action);}
    void keyPressEvent(QKeyEvent* event)override{if(event->key()==Qt::Key_Escape){if(settingsPage)perform(Back);else if(QSystemTrayIcon::isSystemTrayAvailable())hide();else qApp->quit();}if(event->key()==Qt::Key_F5)request();}
    void showEvent(QShowEvent* event)override{QWidget::showEvent(event);syncMotion();}
    void hideEvent(QHideEvent* event)override{ambient.stop();for(auto animation:switchAnimations)animation->stop();QWidget::hideEvent(event);}
    bool event(QEvent* event)override{if(event->type()==QEvent::WindowDeactivate&&!inspect&&QSystemTrayIcon::isSystemTrayAvailable())hide();return QWidget::event(event);}
    void closeEvent(QCloseEvent* event)override{if(QSystemTrayIcon::isSystemTrayAvailable()){event->ignore();hide();}else qApp->quit();}
private:
    QImage photo,number;QString numberText;QColor numberColor;float numberRatio=1;
    QTimer ambient;QElapsedTimer clock,manualClock;bool animations=true,polling=false,warned=false;int hovered=0;
    QVariantAnimation* switchAnimations[3];float switchPositions[3];std::vector<Hit> hits;
    QAction* statusAction;QAction* runtimeAction;
    void syncMotion(){if(isVisible()&&!settingsPage&&animations&&state.reading.percent>=0)ambient.start();else ambient.stop();}
    void request(){if(preview||!refresh)return;if(manualClock.isValid()&&manualClock.elapsed()<1500)return;manualClock.restart();polling=true;refresh();update();}
    void updateTray(){
        QPixmap pixmap(64,64);pixmap.fill(Qt::transparent);QPainter p(&pixmap);p.setRenderHint(QPainter::TextAntialiasing);p.setPen(state.LooksLive(Now())?QColor("#a5f57a"):QColor("#a0aea3"));QFont font("DejaVu Sans");font.setPixelSize(state.reading.percent==100?28:36);font.setWeight(QFont::DemiBold);p.setFont(font);p.drawText(pixmap.rect(),Qt::AlignCenter,state.reading.percent<0?QString::fromUtf8("—"):QString::number(state.reading.percent));p.end();tray.setIcon(QIcon(pixmap));
        QString battery=state.reading.percent<0?"Waiting for mouse":QString("%1% battery").arg(state.reading.percent);statusAction->setText(battery);runtimeAction->setText(runtime());QString tip="Basilisk Pulse · "+battery+"\n"+runtime();
        if(!state.reading.connected&&state.reading.percent>=0)tip+=QString("\nLast reading: %1 min ago").arg(std::max<int64_t>(0,(Now()-state.reading.time)/60));if(!state.reading.error.empty())tip+="\n"+QString::fromStdWString(state.reading.error);tray.setToolTip(tip);
    }
    void perform(int action){
        bool motionBefore=animations;
        switch(action){
        case Refresh:request();return;case Settings:settingsPage=true;break;case Back:settingsPage=false;break;case Exit:qApp->quit();return;
        case About:QMessageBox::information(this,"Basilisk Pulse",QString("Basilisk Pulse %1\nNative Linux preview · MIT\n\nUSB HID access requires the bundled udev rule.\nTray support depends on the desktop environment.\n\n%2").arg(PULSE_VERSION,QString::fromStdWString(state.reading.error)));return;
        case Startup:if(!SetAutostart(!Autostart()))QMessageBox::warning(this,"Autostart","Could not update the user autostart file.");break;
        case Motion:animations=!animations;preferences.setValue("animations",animations);break;
        case Economy:interval=interval==300?120:300;preferences.setValue("economy",interval==300);request();break;
        case Minus:nominal=std::max(5.0,nominal-1);break;case Plus:nominal=std::min(300.0,nominal+1);break;
        case FastPreset:nominal=model().fastPollingHours;break;case MiddlePreset:nominal=60;break;case DefaultPreset:nominal=model().nominalHours;break;
        }
        if(action>=Minus&&action!=Back)preferences.setValue(profileKey(),nominal);
        if(action>=Startup&&action<=Economy){int index=action-Startup;float target=index==0?Autostart():index==1?animations:interval==300;auto animation=switchAnimations[index];animation->stop();if(motionBefore||animations){animation->setStartValue(switchPositions[index]);animation->setEndValue(target);animation->start();}else switchPositions[index]=target;}
        syncMotion();updateTray();update();
    }
};
class Poller {
public:
    explicit Poller(Panel& view):panel(view){panel.refresh=[this]{{std::lock_guard<std::mutex> lock(mutex);requested=true;}condition.notify_one();};thread=std::thread([this,initial=view.state.reading]{run(initial);});}
    ~Poller(){{std::lock_guard<std::mutex> lock(mutex);stopping=true;}condition.notify_one();thread.join();}
private:
    Panel& panel;std::thread thread;std::mutex mutex;std::condition_variable condition;bool stopping=false,requested=true;
    void run(Reading initial){
        TelemetryState state;state.Restore(initial);History history;const ModelInfo* historyModel=nullptr;unsigned delay=0,retries=0;
        while(true){
            {std::unique_lock<std::mutex> lock(mutex);condition.wait_for(lock,std::chrono::seconds(delay),[this]{return stopping||requested;});if(stopping)return;requested=false;}
            auto reading=ReadBattery();state.Apply(reading);
            if(reading.connected){auto model=ModelForPid(reading.pid);if(model!=historyModel){history.samples.clear();history.Load(DataPath(model->historyFile));historyModel=model;}history.Add(reading);history.Save(DataPath(model->historyFile));SaveLastReading(DataPath(L"last-reading.tsv"),reading);}
            auto estimate=history.Predict(state.reading.percent);retries=reading.connected&&reading.charging>=0?0:std::min(retries+1,10000u);delay=RetryDelay(retries,panel.interval.load());
            QMetaObject::invokeMethod(&panel,[this,reading,estimate]{panel.acceptReading(reading,estimate);},Qt::QueuedConnection);
        }
    }
};
}
int main(int argc,char** argv){
    for(int i=1;i<argc;++i){if(std::string(argv[i])=="--version"){std::cout<<"Basilisk Pulse "<<PULSE_VERSION<<"\n";return 0;}if(std::string(argv[i])=="--probe"){auto r=ReadBattery(true);std::cout<<"percent="<<r.percent<<" connected="<<r.connected<<"\n";return r.connected?0:1;}}
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);QApplication app(argc,argv);app.setApplicationName("BasiliskPulse");app.setApplicationVersion(PULSE_VERSION);app.setQuitOnLastWindowClosed(false);
    auto args=app.arguments();bool snapshot=args.contains("--snapshot");QString serverName="BasiliskPulse-"+qEnvironmentVariable("UID",qEnvironmentVariable("USER","user"));
    QLocalServer server;if(!snapshot){QLocalSocket socket;socket.connectToServer(serverName);if(socket.waitForConnected(300)){socket.write("show");socket.waitForBytesWritten(300);return 0;}QLocalServer::removeServer(serverName);if(!server.listen(serverName)){std::cerr<<"Cannot create user session socket\n";return 1;}}
    Panel panel(args.contains("--inspect")||snapshot,snapshot);QObject::connect(&server,&QLocalServer::newConnection,&panel,[&]{while(server.hasPendingConnections()){auto socket=server.nextPendingConnection();socket->deleteLater();}panel.openPanel();});
    if(snapshot){panel.openPanel();QTimer::singleShot(800,&panel,[&]{int index=args.indexOf("--snapshot");QString output=index+1<args.size()?args[index+1]:"panel.png";bool ok=panel.grab().save(output);app.exit(ok?0:1);});return app.exec();}
    Poller poller(panel);QFileSystemWatcher devices;devices.addPath("/dev");QObject::connect(&devices,&QFileSystemWatcher::directoryChanged,&panel,[&]{if(panel.refresh)panel.refresh();});
    if(args.contains("--show")||args.contains("--inspect")||!QSystemTrayIcon::isSystemTrayAvailable())panel.openPanel();
    return app.exec();
}

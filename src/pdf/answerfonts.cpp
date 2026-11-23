#include "answerfonts.h"
#include "runtimepaths.h"
#include <QFontDatabase>
#include <QMutex>
#include <QMutexLocker>
#include <cmath>

bool AnswerFonts::supported(const QString& family) { return family==learning || family==legacy; }
QStringList AnswerFonts::families(const QString& family) {
    if(family==learning) return {QStringLiteral("Langlern Sans"),QStringLiteral("Langlern Kai"),legacy};
    return {legacy};
}
QFont AnswerFonts::font(const QString& family,double pixelSize) {
    registerAll();
    QFont f;
    f.setFamilies(families(family));
    f.setPixelSize(std::max(1,int(std::lround(pixelSize))));
    return f;
}
QStringList AnswerFonts::files() {
    const auto dir=runtimeDataDir()+QStringLiteral("/fonts/");
    return {dir+QStringLiteral("NotoSansSC-Regular.ttf"),dir+QStringLiteral("LanglernSans-Regular.ttf"),dir+QStringLiteral("LanglernSans-Bold.ttf"),
        dir+QStringLiteral("LanglernKai-Regular.ttf"),dir+QStringLiteral("LanglernKai-Medium.ttf")};
}
bool AnswerFonts::registerAll() {
    static QMutex mutex; static int state=0; // 0 not tried, 1 ok, -1 failed
    QMutexLocker lock(&mutex);
    if(state==0) {
        state=1;
        for(const auto& file:files()) if(QFontDatabase::addApplicationFont(file)<0) state=-1;
    }
    return state==1;
}

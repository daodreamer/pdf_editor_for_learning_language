#include "exporttext.h"
#include <QCoreApplication>
#include <QFile>
#include <QFont>
#include <QGlyphRun>
#include <QRawFont>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QAbstractTextDocumentLayout>
#include <QSet>
#include <cpp/fpdf_scopers.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
QByteArray hex16(quint32 value) { return QByteArray::number(value,16).rightJustified(4,'0'); }
QByteArray utf16Hex(const QString& text) {
    QByteArray result; for(auto c:text) result+=hex16(c.unicode()); return result;
}
}
ExportText prepareExportText(const QList<TextAnnotation>& notes) {
    ExportText result;
    if(notes.isEmpty()) return result;
    QFile file(QCoreApplication::applicationDirPath()+"/fonts/NotoSansSC-Regular.ttf");
    check(file.open(QIODevice::ReadOnly),"缺少导出字体文件。"); result.font=file.readAll();
    QMap<QByteArray,uint32_t> codes; QList<QByteArray> unicode;
    result.cidToGid=QByteArray(2,0); // CID zero reserved.
    auto ordered=notes;
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) { return a.order<b.order; });
    for(const auto& note:ordered) {
        if(note.text.isEmpty()) continue;
        check(note.family==QStringLiteral("Noto Sans SC"),"不支持的答案字体。");
        QFont font(note.family); font.setPixelSize(qRound(note.fontSize));
        const auto raw=QRawFont::fromFont(font);
        check(raw.isValid() && raw.familyName()==note.family,"受控答案字体无法加载。");
        for(auto cp:note.text.toUcs4()) check(QChar::isSpace(cp) || raw.supportsCharacter(cp),"答案包含 Noto 字体不支持的字符，未导出。");
        QTextDocument document; document.setDocumentMargin(0); document.setDefaultFont(font);
        QTextOption option; option.setAlignment(Qt::AlignLeft); option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        document.setDefaultTextOption(option); document.setPlainText(note.text); document.setTextWidth(note.width);
        (void)document.documentLayout()->documentSize();
        ExportNote out{note,{}};
        for(auto block=document.begin();block.isValid();block=block.next()) {
            const auto* layout=block.layout(); const auto text=block.text();
            const auto runs=layout->glyphRuns(0,int(text.size()),QTextLayout::RetrieveAll);
            QList<qsizetype> boundaries{text.size()};
            for(const auto& run:runs) for(auto index:run.stringIndexes()) boundaries.append(index);
            std::sort(boundaries.begin(),boundaries.end()); boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end());
            QSet<qsizetype> emitted;
            for(const auto& run:runs) {
                const auto glyphs=run.glyphIndexes(); const auto positions=run.positions(); const auto indexes=run.stringIndexes();
                check(run.rawFont().familyName()==note.family,"排版发生未授权字体回退，未导出。");
                check(glyphs.size()==positions.size() && glyphs.size()==indexes.size(),"字形布局不完整。");
                for(qsizetype i=0;i<glyphs.size();++i) {
                    const auto index=indexes[i]; const auto end=std::upper_bound(boundaries.begin(),boundaries.end(),index);
                    check(index>=0 && index<text.size() && end!=boundaries.end() && glyphs[i]<65536 && glyphs[i]>0,"无法映射导出字形。");
                    const auto chars=emitted.contains(index)?QString(QChar(0x200b)):text.mid(index,*end-index); emitted.insert(index);
                    const auto u=utf16Hex(chars); const auto key=QByteArray::number(glyphs[i])+':'+u;
                    uint32_t cid=codes.value(key,0);
                    if(!cid) {
                        check(codes.size()<65534,"导出字形映射超过上限，请拆分练习。");
                        cid=uint32_t(codes.size()+1); codes[key]=cid; unicode.append(u);
                        result.cidToGid.append(char(glyphs[i]>>8)); result.cidToGid.append(char(glyphs[i]&255));
                    }
                    out.glyphs.append({cid,positions[i]+layout->position()});
                }
            }
        }
        result.notes.append(std::move(out));
    }
    result.cmap="/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n/CMapName /LanglernUTF16 def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
    for(qsizetype start=0;start<unicode.size();start+=100) {
        const auto count=std::min(qsizetype(100),unicode.size()-start); result.cmap+=QByteArray::number(count)+" beginbfchar\n";
        for(qsizetype j=start;j<start+count;++j) result.cmap+='<'+hex16(quint32(j+1))+"> <"+unicode[j]+">\n";
        result.cmap+="endbfchar\n";
    }
    result.cmap+="endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    return result;
}
void insertExportText(FPDF_DOCUMENT document,FPDF_PAGE page,FPDF_FONT font,const ExportNote& out) {
    const auto& n=out.note;
    for(qsizetype start=0;start<out.glyphs.size();) {
        const auto first=out.glyphs[start];
        qsizetype end=start+1;
        while(end<out.glyphs.size() && std::abs(out.glyphs[end].baseline.y()-first.baseline.y())<1e-6) ++end;
        QList<uint32_t> codes; QList<float> positions;
        for(qsizetype i=start;i<end;++i) {
            codes.append(out.glyphs[i].cid);
            if(i>start) positions.append(float(out.glyphs[i].baseline.x()-first.baseline.x()));
        }
        ScopedFPDFPageObject object(FPDFPageObj_CreateTextObj(document,font,float(qRound(n.fontSize))));
        check(bool(object),"创建导出文字失败。"); check(FPDFText_SetCharcodes(object.get(),codes.constData(),size_t(codes.size())),"导出字形写入失败。");
        if(!positions.isEmpty()) check(FPDFText_SetPositions(object.get(),positions.constData(),size_t(positions.size())),"导出字形位置失败。");
        const auto baseline=n.anchor+n.right*first.baseline.x()+n.down*first.baseline.y();
        FPDFPageObj_Transform(object.get(),n.right.x(),n.right.y(),-n.down.x(),-n.down.y(),baseline.x(),baseline.y());
        check(FPDFPageObj_SetFillColor(object.get(),n.color.red(),n.color.green(),n.color.blue(),n.color.alpha()),"导出文字颜色失败。");
        FPDFPage_InsertObject(page,object.release());
        start=end;
    }
}

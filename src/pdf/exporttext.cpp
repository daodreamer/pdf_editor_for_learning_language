#include "exporttext.h"
#include "runtimepaths.h"
#include "textstyles.h"
#include "answerfonts.h"
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
    check(AnswerFonts::registerAll(),"缺少导出字体文件。");
    // Every bundled face, identified by its head/maxp tables so each laid-out glyph run
    // maps to exactly one embedded file.
    struct Face { QByteArray data, head, maxp; QRawFont raw; QMap<QByteArray,uint32_t> codes; QList<QByteArray> unicode; QByteArray cidToGid=QByteArray(2,0); };
    QList<Face> faces;
    for(const auto& path:AnswerFonts::files()) {
        QFile file(path); check(file.open(QIODevice::ReadOnly),"缺少导出字体文件。");
        Face face; face.data=file.readAll(); face.raw=QRawFont(face.data,14,QFont::PreferDefaultHinting);
        check(face.raw.isValid(),"内嵌字体文件无效。");
        face.head=face.raw.fontTable("head"); face.maxp=face.raw.fontTable("maxp");
        faces.append(std::move(face));
    }
    auto faceOf=[&](const QRawFont& raw) {
        const auto head=raw.fontTable("head"),maxp=raw.fontTable("maxp");
        for(qsizetype i=0;i<faces.size();++i) if(faces[i].head==head && faces[i].maxp==maxp) return int(i);
        return -1;
    };
    auto ordered=notes;
    std::sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) { return a.order<b.order; });
    for(const auto& note:ordered) {
        if(note.text.isEmpty()) continue;
        check(AnswerFonts::supported(note.family),"不支持的答案字体。");
        const auto font=AnswerFonts::font(note.family,note.fontSize);
        // Characters none of the note's faces can draw would fall back to a system font
        // that is not embedded; refuse them up front with a clear message.
        // files() lists the legacy Noto face first; the learning set may use every face.
        QList<int> allowed{0};
        if(note.family==AnswerFonts::learning) for(int i=1;i<int(faces.size());++i) allowed.append(i);
        for(auto cp:note.text.toUcs4()) {
            bool drawable=QChar::isSpace(cp);
            for(int i:allowed) drawable=drawable || faces[i].raw.supportsCharacter(cp);
            check(drawable,"答案包含随附字体不支持的字符，未导出。");
        }
        QTextDocument document; document.setDocumentMargin(0); document.setDefaultFont(font);
        QTextOption option; option.setAlignment(Qt::AlignLeft); option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        document.setDefaultTextOption(option); document.setPlainText(note.text); document.setTextWidth(note.width);
        applyTextStyles(document,note.styles);
        applyLineSpacing(document,note.lineSpacing);
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
                const int index=faceOf(run.rawFont());
                check(index>=0 && allowed.contains(index),"排版发生未授权字体回退，未导出；请使用应用随附字体。");
                auto& face=faces[index];
                const bool regularFace=face.raw.weight()<QFont::Medium;
                check(glyphs.size()==positions.size() && glyphs.size()==indexes.size(),"字形布局不完整。");
                // In lines that mix faces, QGlyphRun::stringIndexes can point a glyph of one
                // face at a character drawn by another (seen with Qt 6.8: a space attributed to
                // the following 中). The glyph would then export with the wrong Unicode text,
                // so it is re-attached to the nearest unclaimed character it really draws.
                auto drawsCharacter=[&](qsizetype p,quint32 glyph) {
                    const auto mapped=face.raw.glyphIndexesForString(text.mid(p,1));
                    return mapped.size()==1 && mapped.first()==glyph;
                };
                for(qsizetype i=0;i<glyphs.size();++i) {
                    auto at=indexes[i];
                    if(at>=0 && at<text.size() && !text.at(at).isSurrogate() && !drawsCharacter(at,glyphs[i])
                        && face.raw.glyphIndexesForString(text.mid(at,1)).value(0)==0) {
                        for(qsizetype d=1;d<text.size();++d) {
                            if(at+d<text.size() && !emitted.contains(at+d) && drawsCharacter(at+d,glyphs[i])) { at+=d; break; }
                            if(at-d>=0 && !emitted.contains(at-d) && drawsCharacter(at-d,glyphs[i])) { at-=d; break; }
                        }
                    }
                    const auto end=std::upper_bound(boundaries.begin(),boundaries.end(),at);
                    check(at>=0 && at<text.size() && end!=boundaries.end() && glyphs[i]<65536 && glyphs[i]>0,"无法映射导出字形。");
                    const auto chars=emitted.contains(at)?QString(QChar(0x200b)):text.mid(at,*end-at); emitted.insert(at);
                    const auto u=utf16Hex(chars); const auto key=QByteArray::number(glyphs[i])+':'+u;
                    uint32_t cid=face.codes.value(key,0);
                    if(!cid) {
                        check(face.codes.size()<65534,"导出字形映射超过上限，请拆分练习。");
                        cid=uint32_t(face.codes.size()+1); face.codes[key]=cid; face.unicode.append(u);
                        face.cidToGid.append(char(glyphs[i]>>8)); face.cidToGid.append(char(glyphs[i]&255));
                    }
                    TextStyle style; style.size=note.fontSize;style.color=note.color;
                    const int character=block.position()+int(at);
                    auto styled=std::upper_bound(note.styles.cbegin(),note.styles.cend(),character,[](int p,const TextStyle& s){return p<s.start;});
                    if(styled!=note.styles.cbegin()) { --styled; if(character<styled->start+styled->length)style=*styled; }
                    out.glyphs.append({index,cid,positions[i]+layout->position(),style,character,style.bold && regularFace});
                }
            }
        }
        std::stable_sort(out.glyphs.begin(),out.glyphs.end(),[](const auto& a,const auto& b){return a.character<b.character;});
        result.notes.append(std::move(out));
    }
    // Embed only the faces that drew something; renumber glyph font indexes to match.
    QList<int> slot(faces.size(),-1);
    for(qsizetype i=0;i<faces.size();++i) {
        if(faces[i].codes.isEmpty()) continue;
        slot[i]=int(result.fonts.size());
        ExportFont embedded{faces[i].data,{},faces[i].cidToGid};
        auto& cmap=embedded.cmap;
        cmap="/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n/CMapName /LanglernUTF16-"
            +QByteArray::number(slot[i])+" def\n/CMapType 2 def\n1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
        const auto& unicode=faces[i].unicode;
        for(qsizetype start=0;start<unicode.size();start+=100) {
            const auto count=std::min(qsizetype(100),unicode.size()-start); cmap+=QByteArray::number(count)+" beginbfchar\n";
            for(qsizetype j=start;j<start+count;++j) cmap+='<'+hex16(quint32(j+1))+"> <"+unicode[j]+">\n";
            cmap+="endbfchar\n";
        }
        cmap+="endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
        result.fonts.append(std::move(embedded));
    }
    for(auto& note:result.notes) for(auto& glyph:note.glyphs) glyph.font=slot[glyph.font];
    return result;
}
void insertExportText(FPDF_DOCUMENT document,FPDF_PAGE page,const QList<FPDF_FONT>& fonts,const ExportNote& out) {
    const auto& n=out.note;
    for(qsizetype start=0;start<out.glyphs.size();) {
        const auto first=out.glyphs[start];
        qsizetype end=start+1;
        while(end<out.glyphs.size() && out.glyphs[end].font==first.font && out.glyphs[end].syntheticBold==first.syntheticBold
            && out.glyphs[end].style==first.style && std::abs(out.glyphs[end].baseline.y()-first.baseline.y())<1e-6) ++end;
        QList<uint32_t> codes; QList<float> positions;
        for(qsizetype i=start;i<end;++i) {
            codes.append(out.glyphs[i].cid);
            if(i>start) positions.append(float(out.glyphs[i].baseline.x()-first.baseline.x()));
        }
        check(first.font>=0 && first.font<fonts.size() && fonts[first.font],"导出字体缺失。");
        ScopedFPDFPageObject object(FPDFPageObj_CreateTextObj(document,fonts[first.font],float(qRound(first.style.size))));
        check(bool(object),"创建导出文字失败。"); check(FPDFText_SetCharcodes(object.get(),codes.constData(),size_t(codes.size())),"导出字形写入失败。");
        if(!positions.isEmpty()) check(FPDFText_SetPositions(object.get(),positions.constData(),size_t(positions.size())),"导出字形位置失败。");
        const auto baseline=n.anchor+n.right*first.baseline.x()+n.down*first.baseline.y();
        const auto up=-n.down+(first.style.italic?.2:0)*n.right;
        FPDFPageObj_Transform(object.get(),n.right.x(),n.right.y(),up.x(),up.y(),baseline.x(),baseline.y());
        const auto color=first.style.color;
        check(FPDFPageObj_SetFillColor(object.get(),color.red(),color.green(),color.blue(),color.alpha()),"导出文字颜色失败。");
        if(first.syntheticBold) {
            check(FPDFTextObj_SetTextRenderMode(object.get(),FPDF_TEXTRENDERMODE_FILL_STROKE),"导出粗体失败。");
            check(FPDFPageObj_SetStrokeColor(object.get(),color.red(),color.green(),color.blue(),color.alpha()),"导出粗体颜色失败。");
            check(FPDFPageObj_SetStrokeWidth(object.get(),float(first.style.size/40)),"导出粗体线宽失败。");
        }
        FPDFPage_InsertObject(page,object.release());
        start=end;
    }
}

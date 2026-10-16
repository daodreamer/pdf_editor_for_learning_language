#include "scancleanup.h"
#include <QPainter>
#include <cmath>
#include <algorithm>
#include <vector>

QImage cleanScan(QImage source,const QVariantMap& options) {
    auto image=source.convertToFormat(QImage::Format_RGB32);
    if(options.value("crop").toBool() && image.width()>100 && image.height()>100) {
        const auto rowMean=[&](int y){qint64 sum=0;const auto* p=reinterpret_cast<const QRgb*>(image.constScanLine(y));for(int x=0;x<image.width();x+=4)sum+=qGray(p[x]);return double(sum)/((image.width()+3)/4);};
        const auto columnMean=[&](int x){qint64 sum=0;for(int y=0;y<image.height();y+=4)sum+=qGray(image.pixel(x,y));return double(sum)/((image.height()+3)/4);};
        int top=0,bottom=image.height()-1,left=0,right=image.width()-1;
        while(top<image.height()/8 && rowMean(top)<150)++top;
        while(bottom>image.height()*7/8 && rowMean(bottom)<150)--bottom;
        while(left<image.width()/8 && columnMean(left)<150)++left;
        while(right>image.width()*7/8 && columnMean(right)<150)--right;
        if(top||left||bottom!=image.height()-1||right!=image.width()-1)image=image.copy(left,top,right-left+1,bottom-top+1);
    }
    if(options.value("deshadow").toBool()) {
        auto background=image.scaled(48,48,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        auto expanded=background;
        for(int y=0;y<48;++y)for(int x=0;x<48;++x){int value=0;for(int j=std::max(0,y-2);j<=std::min(47,y+2);++j)for(int i=std::max(0,x-2);i<=std::min(47,x+2);++i)value=std::max(value,qGray(background.pixel(i,j)));expanded.setPixel(x,y,qRgb(value,value,value));}
        background=expanded.scaled(image.size(),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        for(int y=0;y<image.height();++y){auto* p=reinterpret_cast<QRgb*>(image.scanLine(y));const auto* bg=reinterpret_cast<const QRgb*>(background.constScanLine(y));
            for(int x=0;x<image.width();++x){const int shade=std::max(100,qGray(bg[x]));p[x]=qRgb(std::min(255,qRed(p[x])*250/shade),std::min(255,qGreen(p[x])*250/shade),std::min(255,qBlue(p[x])*250/shade));}}
    }
    if(options.value("clarity").toBool()) {
        for(int y=0;y<image.height();++y){auto* p=reinterpret_cast<QRgb*>(image.scanLine(y));for(int x=0;x<image.width();++x){const auto adjust=[](int c){return std::clamp((c-128)*115/100+140,0,255);};p[x]=qRgb(adjust(qRed(p[x])),adjust(qGreen(p[x])),adjust(qBlue(p[x])));}}
    }
    if(options.value("straighten").toBool()) {
        const auto small=image.scaled(640,640,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        double best=0,bestScore=-1,zeroScore=0;
        for(int step=-12;step<=12;++step){const double angle=step*.25;const double slope=std::tan(angle*3.141592653589793/180);std::vector<double> rows(size_t(small.height()+80),0);
            for(int y=0;y<small.height();++y)for(int x=0;x<small.width();x+=2){const int gray=qGray(small.pixel(x,y));if(gray<140){const int row=qRound(y+slope*(x-small.width()/2))+40;if(row>=0&&row<int(rows.size()))rows[size_t(row)]+=140-gray;}}
            double score=0;for(double row:rows)score+=row*row;if(step==0)zeroScore=score;if(score>bestScore){bestScore=score;best=angle;}}
        if(std::abs(best)>=.25 && bestScore>zeroScore*1.03){QImage rotated(image.size(),QImage::Format_RGB32);rotated.fill(Qt::white);QPainter p(&rotated);p.setRenderHint(QPainter::SmoothPixmapTransform);p.translate(image.width()/2.,image.height()/2.);p.rotate(best);p.drawImage(QPointF(-image.width()/2.,-image.height()/2.),image);p.end();image=std::move(rotated);}
    }
    return image;
}

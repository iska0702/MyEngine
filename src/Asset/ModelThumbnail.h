#pragma once
#include <vector>
#include <array>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
namespace EngineAsset {
using ThumbnailLines=std::vector<std::array<float,4>>;
inline ThumbnailLines ReadObjThumbnail(const std::filesystem::path& path){
    std::error_code ec;if(std::filesystem::file_size(path,ec)>8*1024*1024||ec)return {};
    std::ifstream file(path);std::vector<std::array<float,3>> vertices;std::vector<std::array<size_t,2>> edges;std::string line;
    while(std::getline(file,line)&&vertices.size()<100000&&edges.size()<1500){
        std::istringstream in(line);std::string tag;in>>tag;
        if(tag=="v"){std::array<float,3> v;if(in>>v[0]>>v[1]>>v[2])if(std::isfinite(v[0])&&std::isfinite(v[1])&&std::isfinite(v[2]))vertices.push_back(v);}
        if(tag=="f"){
            std::vector<size_t> face;std::string item;
            while(in>>item){try{auto raw=std::stoll(item);auto index=raw>0?raw-1:static_cast<long long>(vertices.size())+raw;if(index>=0&&index<static_cast<long long>(vertices.size()))face.push_back(size_t(index));}catch(...){}if(face.size()>=64)break;}
            for(size_t i=0;i<face.size();++i)edges.push_back({face[i],face[(i+1)%face.size()]});
        }
    }
    if(edges.empty())return {};
    std::vector<std::array<float,2>> points;float loX=1e30f,hiX=-1e30f,loY=1e30f,hiY=-1e30f;
    for(auto v:vertices){float x=(v[0]-v[2])*.7071f,y=v[1]*.8165f-(v[0]+v[2])*.4082f;points.push_back({x,y});loX=(std::min)(loX,x);hiX=(std::max)(hiX,x);loY=(std::min)(loY,y);hiY=(std::max)(hiY,y);}
    float size=(std::max)({hiX-loX,hiY-loY,.0001f});ThumbnailLines result;
    for(auto edge:edges){auto a=points[edge[0]],b=points[edge[1]];result.push_back({.5f+(a[0]-(loX+hiX)*.5f)/size,.5f-(a[1]-(loY+hiY)*.5f)/size,.5f+(b[0]-(loX+hiX)*.5f)/size,.5f-(b[1]-(loY+hiY)*.5f)/size});}return result;
}
}

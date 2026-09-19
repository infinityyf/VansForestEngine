#include "VansModelLodBuilder.h"
#include "VansModelLodBuildData.h"
#include "../../AssetCore/VansAssetDatabase.h"
#include "../../AssetCore/Storage/VansAssetMetaStorage.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../Util/VansFileFingerprint.h"
#include <meshoptimizer.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <functional>
#include <sstream>
#include <iomanip>
#include <limits>
#include <cmath>

namespace Vans
{
namespace
{
using Json=nlohmann::ordered_json;
using namespace ModelLodBuild;
std::string Hash(const std::string& bytes)
{
    std::ostringstream stream;stream<<std::hex<<std::setw(16)<<std::setfill('0')<<ComputeMemoryFnv1a64(bytes.data(),bytes.size());return stream.str();
}
VansAssetGuid Guid(const std::string& text)
{
    VansAssetGuid id;if(!VansAssetGuid::TryParse(text,id))throw std::runtime_error("Invalid generated resource GUID.");return id;
}
Json Describe(const VansModelLodAsset& result)
{
    Json levels=Json::array();
    for(const auto& level:result.levels){Json parts=Json::array();for(const auto& p:level.parts)
        parts.push_back({{"model",p.model.ToString()},{"material",p.material.ToString()},{"submesh",p.submesh},{"sourcePart",p.sourcePart},{"triangles",p.triangleCount},{"error",p.error}});
        levels.push_back({{"parts",parts}});}
    return {{"buildKey",result.buildKey},{"centerRadius",result.centerRadius},{"levels",levels}};
}
VansModelLodAsset ReadResult(const Json& value)
{
    VansModelLodAsset r;r.buildKey=value.at("buildKey");r.centerRadius=value.at("centerRadius").get<std::array<float,4>>();
    for(const auto& l:value.at("levels")){VansModelLodLevel level;
        for(const auto& p:l.at("parts"))level.parts.push_back({Guid(p.at("model")),Guid(p.at("material")),p.at("submesh"),p.at("sourcePart"),p.at("triangles"),p.at("error")});
        r.levels.push_back(std::move(level));}return r;
}
bool ReadGeometry(const std::filesystem::path& path,float scale,int submesh,bool multiMesh,Part& part,std::string& error)
{
    Assimp::Importer importer;
    const auto* scene=importer.ReadFile(path.string(),aiProcess_Triangulate|aiProcess_FlipUVs|aiProcess_GenSmoothNormals|
        aiProcess_JoinIdenticalVertices|aiProcess_ImproveCacheLocality|aiProcess_SortByPType|aiProcess_ValidateDataStructure);
    if(!scene || !scene->mRootNode){error=importer.GetErrorString();return false;}
    if(scene->HasAnimations()){error="Model LOD currently requires a static model without animation.";return false;}
    int ordinal=0;bool valid=true;
    std::function<void(const aiNode*,aiMatrix4x4)> visit=[&](const aiNode* node,aiMatrix4x4 parent){
        const auto transform=parent*node->mTransformation;aiMatrix3x3 normalTransform(transform);normalTransform.Inverse().Transpose();
        for(unsigned m=0;m<node->mNumMeshes;++m){
            if(multiMesh && node->mName.length==0)continue;const auto* mesh=scene->mMeshes[node->mMeshes[m]];
            const int index=ordinal++;if(submesh>=0 && submesh!=index)continue;
            if(mesh->HasBones()){valid=false;error="Skinned meshes cannot be passed to the static model LOD builder.";return;}
            const uint32_t base=uint32_t(part.vertices.size());
            for(unsigned i=0;i<mesh->mNumVertices;++i){
                const auto p=(transform*mesh->mVertices[i])*scale;auto n=normalTransform*(mesh->HasNormals()?mesh->mNormals[i]:aiVector3D(0,1,0));n.NormalizeSafe();
                const auto uv=mesh->HasTextureCoords(0)?mesh->mTextureCoords[0][i]:aiVector3D(0,0,0);
                part.vertices.push_back({{p.x,p.y,p.z},{n.x,n.y,n.z},{uv.x,uv.y}});
            }
            for(unsigned f=0;f<mesh->mNumFaces;++f){const auto& face=mesh->mFaces[f];if(face.mNumIndices!=3)continue;
                for(unsigned k=0;k<3;++k)part.indices.push_back(base+face.mIndices[k]);}
        }
        for(unsigned i=0;i<node->mNumChildren;++i)visit(node->mChildren[i],transform);
    };visit(scene->mRootNode,aiMatrix4x4());
    if(!valid)return false;
    if(part.indices.empty()){error="Selected model part has no triangles.";return false;}return true;
}
}
bool VansModelLodBuilder::Build(VansAssetDatabase& database,
    const std::vector<VansModelLodSourcePart>& sources,const VansModelLodSettings& settings,VansModelLodAsset& result,std::string& error)
{
    error.clear();
    try {
        if(sources.empty() || sources.size()>64 || !std::isfinite(settings.maximumError) || settings.maximumError<=0 || settings.maximumError>.25f ||
            !std::isfinite(settings.ratios[0]) || !std::isfinite(settings.ratios[1]) || settings.ratios[0]>=1 || settings.ratios[1]<=0 || settings.ratios[1]>=settings.ratios[0]) {error="Invalid model LOD ratios or error limit.";return false;}
        VansScopedIOContext io(VansIODomain::Authoring,"Editor.BuildModelLod",true);
        Json fingerprint={{"compiler",VANS_MODEL_LOD_COMPILER_FINGERPRINT},{"ratios",settings.ratios},{"maximumError",settings.maximumError}};
        std::vector<Part> parts(sources.size());std::vector<std::filesystem::path> paths;std::vector<float> scales;std::vector<bool> multiMeshes;std::vector<int> selections;
        std::vector<std::pair<std::filesystem::path,uint64_t>> inputs;
        const auto fileHash=[&](const std::filesystem::path& path){VansFileFingerprint f;if(!ComputeFileFingerprint(path,f,&error))throw std::runtime_error(error);return f.contentHash;};
        for(size_t i=0;i<sources.size();++i){const auto& source=sources[i];
            const auto model=database.Find(source.model),material=database.Find(source.material);
            if(!model || model->type!=VansAssetType::Model || !material || material->type!=VansAssetType::Material || source.submesh< -1)
                throw std::runtime_error("Model LOD requires valid model and material references for every part.");
            VansAssetMeta meta;if(!VansAssetMetaStorage::Load(model->metaPath,meta,error))return false;
            const float scale=meta.ReadFloatSetting("scaleFactor","scale",1.f);
            if(!std::isfinite(scale)||scale<=0)throw std::runtime_error("Invalid model scale.");
            paths.push_back(model->sourcePath);scales.push_back(scale);
            const bool multi=meta.ReadBoolSetting("loadMultiMesh",false);
            if(!multi && source.submesh>0)throw std::runtime_error("Single-mesh import only has submesh 0.");
            multiMeshes.push_back(multi);selections.push_back(multi?source.submesh:-1);
            Json sourceHash={{"model",source.model.ToString()},{"source",fileHash(model->sourcePath)},{"meta",fileHash(model->metaPath)},
                {"material",source.material.ToString()},{"submesh",source.submesh},{"alphaTest",source.alphaTest}};
            inputs.emplace_back(model->sourcePath,sourceHash["source"].get<uint64_t>());
            inputs.emplace_back(model->metaPath,sourceHash["meta"].get<uint64_t>());
            if(model->sourcePath.extension()==".gltf") {
                std::string jsonBytes;if(!VansFileStorage::ReadAllBytes(model->sourcePath,jsonBytes,error))return false;
                const auto gltf=Json::parse(jsonBytes);
                for(const auto& buffer:gltf.value("buffers",Json::array())) {
                    std::string uri=buffer.value("uri",std::string{});
                    if(uri.empty() || uri.rfind("data:",0)==0)continue;
                    // 与 glTF 文件 URI 一致，解码相对路径中的百分号字符。
                    std::string decoded;
                    for(size_t c=0;c<uri.size();++c) {
                        if(uri[c]=='%' && c+2<uri.size()) {decoded+=char(std::stoul(uri.substr(c+1,2),nullptr,16));c+=2;}
                        else decoded+=uri[c];
                    }
                    const auto dependency=model->sourcePath.parent_path()/std::filesystem::u8path(decoded);
                    const auto hash=fileHash(dependency);inputs.emplace_back(dependency,hash);
                    sourceHash["buffers"].push_back({{"uri",uri},{"hash",hash}});
                }
            }
            fingerprint["parts"].push_back(std::move(sourceHash));
        }
        const std::string key=Hash(fingerprint.dump());
        const auto directory=database.AssetsRoot()/"Generated"/"ModelLOD"/key;
        const auto manifestPath=directory/"build.json";
        std::string bytes;Json manifest;bool cached=false;
        if(std::filesystem::is_regular_file(manifestPath)&&VansFileStorage::ReadAllBytes(manifestPath,bytes,error)){
            manifest=Json::parse(bytes,nullptr,false);
            cached=manifest.is_object()&&manifest.value("fingerprint",Json{})==fingerprint&&manifest.contains("files")&&manifest.contains("result");
            if(cached)for(const auto& file:manifest["files"]){const std::string name=file.at("name");
                if(std::filesystem::path(name).filename()!=std::filesystem::path(name)||!std::filesystem::is_regular_file(directory/name)||fileHash(directory/name)!=file.at("hash").get<uint64_t>()){cached=false;break;}}
        }
        error.clear();VansModelLodAsset built;
        if(cached)built=ReadResult(manifest.at("result"));
        else {
            glm::vec3 lo(std::numeric_limits<float>::max()),hi(-std::numeric_limits<float>::max());
            for(size_t i=0;i<parts.size();++i){if(!ReadGeometry(paths[i],scales[i],selections[i],multiMeshes[i],parts[i],error))return false;
                for(const auto& v:parts[i].vertices){lo=glm::min(lo,v.position);hi=glm::max(hi,v.position);}}
            const glm::vec3 center=(lo+hi)*.5f;float radius=0;
            for(const auto& part:parts)for(const auto& v:part.vertices)radius=std::max(radius,glm::length(v.position-center));
            radius*=1.04f;built.buildKey=key;built.centerRadius={center.x,center.y,center.z,radius};
            VansStagedFileTransaction transaction;Json files=Json::array();
            const auto stage=[&](const std::string& name,const std::string& data){
                VansStagedFile file;if(!VansFileStorage::StageWriteBytes(directory/name,data,file,error))throw std::runtime_error(error);
                transaction.Add(std::move(file));files.push_back({{"name",name},{"hash",ComputeMemoryFnv1a64(data.data(),data.size())}});
            };
            const auto resource=[&](const std::string& name,const std::string& data,VansAssetType type,Json importSettings){
                const auto id=VansAssetGuid::FromStableName("Forest.ModelLOD",key+"/"+name);stage(name,data);
                Json meta={{"guid",id.ToString()},{"importer",VansAssetDatabase::ImporterFor(type)},{"version",1},{"settings",importSettings},{"subAssets",Json::object()}};
                stage(name+".meta",meta.dump(2));return id;
            };
            const Json modelSettings={{"loadMultiMesh",true},{"scaleFactor",1},{"generateTangents",true},{"buildRayTracingData",false},{"keepCpuMeshData",false}};
            for(size_t level=0;level<settings.ratios.size();++level){VansModelLodLevel lod;
                for(size_t i=0;i<parts.size();++i){const auto& part=parts[i];std::vector<uint32_t> indices(part.indices.size());float errorValue=0;
                    const float weights[]={.25f,.25f,.25f,.1f,.1f};
                    size_t count=meshopt_simplifyWithAttributes(indices.data(),part.indices.data(),part.indices.size(),&part.vertices[0].position.x,part.vertices.size(),sizeof(Vertex),
                        &part.vertices[0].normal.x,sizeof(Vertex),weights,5,nullptr,std::max(size_t(3),size_t(part.indices.size()*settings.ratios[level])/3*3),
                        settings.maximumError,meshopt_SimplifyPrune,&errorValue);
                    if(count<3)throw std::runtime_error("Simplification removed an entire model part.");indices.resize(count);
                    meshopt_optimizeVertexCache(indices.data(),indices.data(),count,part.vertices.size());
                    std::vector<Vertex> vertices(part.vertices.size());const size_t verticesUsed=meshopt_optimizeVertexFetch(vertices.data(),indices.data(),count,part.vertices.data(),part.vertices.size(),sizeof(Vertex));vertices.resize(verticesUsed);
                    const auto id=resource("lod"+std::to_string(level+1)+"_part"+std::to_string(i)+".glb",EncodeGlb(vertices,indices),VansAssetType::Model,modelSettings);
                    lod.parts.push_back({id,sources[i].material,0,uint32_t(i),uint32_t(count/3),errorValue});
                }built.levels.push_back(std::move(lod));
            }
            manifest={{"fingerprint",fingerprint},{"files",files},{"result",Describe(built)}};stage("build.json",manifest.dump(2));
            for(const auto& input:inputs)if(fileHash(input.first)!=input.second)
                throw std::runtime_error("Model source changed during LOD construction; retry the build.");
            if(!transaction.Publish(error))return false;
        }
        // 先完整发布不可变产物，再注册引用；调用者最后更新自己的作者文档。
        for(const auto& file:manifest.at("files")){
            const auto path=directory/file.at("name").get<std::string>();if(path.extension()==".meta")continue;
            if(!database.RegisterOrRefresh(path,VansAssetOperationPolicy::Cooking(),error))return false;

        }
        result=std::move(built);return true;
    }catch(const std::exception& exception){error=exception.what();return false;}
}
}

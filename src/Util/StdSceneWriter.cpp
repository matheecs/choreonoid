#include "StdSceneWriter.h"
#include "YAMLWriter.h"
#include "SceneGraph.h"
#include "SceneDrawables.h"
#include "SceneGraphOptimizer.h"
#include "PolymorphicSceneNodeFunctionSet.h"
#include "EigenArchive.h"
#include "FilePathVariableProcessor.h"
#include "CloneMap.h"
#include <cnoid/stdx/filesystem>
#include <fmt/format.h>
#include "gettext.h"

using namespace std;
using namespace cnoid;
using fmt::format;
namespace filesystem = cnoid::stdx::filesystem;

namespace cnoid {

class StdSceneWriter::Impl
{
public:
    StdSceneWriter* self;
    PolymorphicSceneNodeFunctionSet writeFunctions;
    MappingPtr currentArchive;
    bool isDegreeMode;
    bool isTransformIntegrationEnabled;
    bool doEmbedAllMeshes;
    int vertexPrecision;
    string vertexFormat;
    SgMaterialPtr defaultMaterial;
    FilePathVariableProcessorPtr pathVariableProcessor;
    unique_ptr<YAMLWriter> yamlWriter;

    Impl(StdSceneWriter* self);
    YAMLWriter* getOrCreateYamlWriter();
    FilePathVariableProcessor* getOrCreatePathVariableProcessor();
    void setBaseDirectory(const std::string& directory);
    bool writeScene(const std::string& filename, SgNode* node, const std::vector<SgNode*>* pnodes);
    MappingPtr writeSceneNode(SgNode* node);
    void writeObjectHeader(Mapping* archive, const char* typeName, SgObject* object);
    void writeGroup(Mapping* archive, SgGroup* group);
    void writePosTransform(Mapping* archive, SgPosTransform* transform);
    void writeScaleTransform(Mapping* archive, SgScaleTransform* transform);
    void writeShape(Mapping* archive, SgShape* shape);
    MappingPtr writeGeometry(SgMesh* mesh);
    void writeMeshAttributes(Mapping* archive, SgMesh* mesh);
    bool writeMesh(Mapping* archive, SgMesh* mesh);
    void writeBox(Mapping* archive, const SgMesh::Box& box);
    void writeSphere(Mapping* archive, const SgMesh::Sphere& sphere);
    void writeCylinder(Mapping* archive, const SgMesh::Cylinder& cylinder);
    void writeCone(Mapping* archive, const SgMesh::Cone& cone);
    void writeCapsule(Mapping* archive, const SgMesh::Capsule& capsule);
    MappingPtr writeAppearance(SgShape* shape);
    MappingPtr writeMaterial(SgMaterial* material);
};

}

StdSceneWriter::StdSceneWriter()
{
    impl = new Impl(this);
    setVertexPrecision(7);
}


StdSceneWriter::Impl::Impl(StdSceneWriter* self)
    : self(self)
{
    writeFunctions.setFunction<SgGroup>(
        [&](SgGroup* group){ writeGroup(currentArchive, group); });
    writeFunctions.setFunction<SgPosTransform>(
        [&](SgPosTransform* transform){ writePosTransform(currentArchive, transform); });
    writeFunctions.setFunction<SgScaleTransform>(
        [&](SgScaleTransform* transform){ writeScaleTransform(currentArchive, transform); });
    writeFunctions.setFunction<SgShape>(
        [&](SgShape* shape){ writeShape(currentArchive, shape); });

    writeFunctions.updateDispatchTable();

    isDegreeMode = true;
    isTransformIntegrationEnabled = false;
}


StdSceneWriter::~StdSceneWriter()
{
    delete impl;
}


YAMLWriter* StdSceneWriter::Impl::getOrCreateYamlWriter()
{
    if(!yamlWriter){
        yamlWriter.reset(new YAMLWriter);
        yamlWriter->setKeyOrderPreservationMode(true);
    }
    return yamlWriter.get();
}


FilePathVariableProcessor* StdSceneWriter::Impl::getOrCreatePathVariableProcessor()
{
    if(!pathVariableProcessor){
        pathVariableProcessor = new FilePathVariableProcessor;
    }
    return pathVariableProcessor;
}


void StdSceneWriter::setBaseDirectory(const std::string& directory)
{
    impl->setBaseDirectory(directory);
}


void StdSceneWriter::Impl::setBaseDirectory(const std::string& directory)
{
    getOrCreatePathVariableProcessor()->setBaseDirectory(directory);
}


void StdSceneWriter::setFilePathVariableProcessor(FilePathVariableProcessor* processor)
{
    impl->pathVariableProcessor = processor;
}


void StdSceneWriter::setIndentWidth(int n)
{
    impl->getOrCreateYamlWriter()->setIndentWidth(n);
}


void StdSceneWriter::setTransformIntegrationEnabled(bool on)
{
    impl->isTransformIntegrationEnabled = on;
}


bool StdSceneWriter::isTransformIntegrationEnabled() const
{
    return impl->isTransformIntegrationEnabled;
}


void StdSceneWriter::setVertexPrecision(int precision)
{
    impl->vertexPrecision = precision;
    impl->vertexFormat = format("%.{}g", precision);
}


int StdSceneWriter::vertexPrecision() const
{
    return impl->vertexPrecision;
}


MappingPtr StdSceneWriter::writeScene(SgNode* node)
{
    impl->doEmbedAllMeshes = false;
    return impl->writeSceneNode(node);
}


bool StdSceneWriter::writeScene(const std::string& filename, SgNode* node)
{
    return impl->writeScene(filename, node, nullptr);
}


bool StdSceneWriter::writeScene(const std::string& filename, const std::vector<SgNode*>& nodes)
{
    return impl->writeScene(filename, nullptr, &nodes);
}


bool StdSceneWriter::Impl::writeScene
(const std::string& filename, SgNode* node, const std::vector<SgNode*>* pnodes)
{
    getOrCreateYamlWriter();

    if(!yamlWriter->openFile(filename)){
        return false;
    }

    SgGroupPtr group = new SgGroup;
    if(node){
        group->addChild(node);
    } else if(pnodes){
        for(auto& node : *pnodes){
            group->addChild(node);
        }
    }

    if(isTransformIntegrationEnabled){
        SceneGraphOptimizer optimizer;
        CloneMap cloneMap;
        SgObject::setNonNodeCloning(cloneMap, false);
        group = cloneMap.getClone(group);
        SgObject::setNonNodeCloning(cloneMap, true);
        optimizer.simplifyTransformPathsWithTransformedMeshes(group, cloneMap);
    }
       
    doEmbedAllMeshes = true;
        
    MappingPtr header = new Mapping;
    header->write("format", "choreonoid_scene");
    header->write("format_version", "1.0");
    header->write("angle_unit", "degree");
    
    auto directory = filesystem::path(filename).parent_path().generic_string();
    setBaseDirectory(directory);

    ListingPtr nodeList = new Listing;
    for(auto& node : *group){
        nodeList->append(writeSceneNode(node));
    }
    header->insert("scene", nodeList);
    
    yamlWriter->putNode(header);
    yamlWriter->closeFile();
    
    return true;
}


MappingPtr StdSceneWriter::Impl::writeSceneNode(SgNode* node)
{
    MappingPtr archive = new Mapping;

    currentArchive = archive;
    writeFunctions.dispatch(node);
    currentArchive.reset();

    if(node->isGroupNode()){
        auto group = node->toGroupNode();
        ListingPtr elements = new Listing;
        for(auto& child : *group){
            if(auto childArchive = writeSceneNode(child)){
                elements->append(childArchive);
            }
        }
        if(!elements->empty()){
            archive->insert("elements", elements);
        }
    }
    
    return archive;
}


void StdSceneWriter::Impl::writeObjectHeader(Mapping* archive, const char* typeName, SgObject* object)
{
    if(typeName){
        auto typeNode = new ScalarNode(typeName);
        typeNode->setAsHeaderInMapping();
        archive->insert("type", typeNode);
    }
    if(!object->name().empty()){
        archive->write("name", object->name());
    }
}
    
    
void StdSceneWriter::Impl::writeGroup(Mapping* archive, SgGroup* group)
{
    writeObjectHeader(archive, "Group", group);
}


void StdSceneWriter::Impl::writePosTransform(Mapping* archive, SgPosTransform* transform)
{
    archive->setFloatingNumberFormat("%.12g");
    writeObjectHeader(archive, "Transform", transform);
    AngleAxis aa(transform->rotation());
    if(aa.angle() != 0.0){
        writeDegreeAngleAxis(archive, "rotation", aa);
    }
    Vector3 p(transform->translation());
    if(!p.isZero()){
        write(archive, "translation", p);
    }
}


void StdSceneWriter::Impl::writeScaleTransform(Mapping* archive, SgScaleTransform* transform)
{
    writeObjectHeader(archive, "Transform", transform);
    write(archive, "scale", transform->scale());
}


void StdSceneWriter::Impl::writeShape(Mapping* archive, SgShape* shape)
{
    writeObjectHeader(archive, "Shape", shape);

    if(auto appearance = writeAppearance(shape)){
        archive->insert("appearance", appearance);
    }
    if(auto geometry = writeGeometry(shape->mesh())){
        archive->insert("geometry", geometry);
    }
}


MappingPtr StdSceneWriter::Impl::writeGeometry(SgMesh* mesh)
{
    if(!mesh){
        return nullptr;
    }

    MappingPtr archive = new Mapping;

    bool doWriteMesh = true;

    if(!mesh->uri().empty() && !doEmbedAllMeshes){
        archive->write("type", "Resource");
        archive->write(
            "uri",
            getOrCreatePathVariableProcessor()->parameterize(mesh->uri()), DOUBLE_QUOTED);
        writeMeshAttributes(archive, mesh);

    } else {
        switch(mesh->primitiveType()){
        case SgMesh::MeshType:
            if(!writeMesh(archive, mesh)){
                archive.reset();
            }
            break;
        case SgMesh::BoxType:
            writeBox(archive, mesh->primitive<SgMesh::Box>());
            break;
        case SgMesh::SphereType:
            writeSphere(archive, mesh->primitive<SgMesh::Sphere>());
            break;
        case SgMesh::CylinderType:
            writeCylinder(archive, mesh->primitive<SgMesh::Cylinder>());
            break;
        case SgMesh::ConeType:
            writeCone(archive, mesh->primitive<SgMesh::Cone>());
            break;
        case SgMesh::CapsuleType:
            writeCapsule(archive, mesh->primitive<SgMesh::Capsule>());
            break;
        default:
            archive = nullptr;
            break;
        }
    }

    return archive;
}


void StdSceneWriter::Impl::writeMeshAttributes(Mapping* archive, SgMesh* mesh)
{
    if(mesh->creaseAngle() > 0.0f){
        archive->write("crease_angle", degree(mesh->creaseAngle()));
    }
    if(mesh->isSolid()){
        archive->write("solid", true);
    }
}


bool StdSceneWriter::Impl::writeMesh(Mapping* archive, SgMesh* mesh)
{
    bool isValid = false;
    int numTriangles = mesh->numTriangles();

    if(mesh->hasVertices() && numTriangles > 0){

        archive->write("type", "TriangleMesh");

        writeMeshAttributes(archive, mesh);

        auto vertices = archive->createFlowStyleListing("vertices");
        vertices->setFloatingNumberFormat(vertexFormat.c_str());
        auto srcVertices = mesh->vertices();
        const int scalarElementSize = srcVertices->size() * 3;
        vertices->reserve(scalarElementSize);
        for(auto& v : *srcVertices){
            vertices->append(v.x(), 12, scalarElementSize);
            vertices->append(v.y(), 12, scalarElementSize);
            vertices->append(v.z(), 12, scalarElementSize);
        }
        
        Listing& indexList = *archive->createFlowStyleListing("triangles");
        const int numTriScalars = numTriangles * 3;
        indexList.reserve(numTriScalars);
        for(int i=0; i < numTriangles; ++i){
            auto triangle = mesh->triangle(i);
            indexList.append(triangle[0], 15, numTriScalars);
            indexList.append(triangle[1], 15, numTriScalars);
            indexList.append(triangle[2], 15, numTriScalars);
        }
        
        isValid = true;
    }
    
    return isValid;
}


void StdSceneWriter::Impl::writeBox(Mapping* archive, const SgMesh::Box& box)
{
    archive->write("type", "Box");
    write(archive, "size", box.size);
}


void StdSceneWriter::Impl::writeSphere(Mapping* archive, const SgMesh::Sphere& sphere)
{
    archive->write("type", "Sphere");
    archive->write("radius", sphere.radius);
}


void StdSceneWriter::Impl::writeCylinder(Mapping* archive, const SgMesh::Cylinder& cylinder)
{
    archive->write("type", "Cylinder");
    archive->write("radius", cylinder.radius);
    archive->write("height", cylinder.height);
}


void StdSceneWriter::Impl::writeCone(Mapping* archive, const SgMesh::Cone& cone)
{
    archive->write("type", "Cone");
    archive->write("radius", cone.radius);
    archive->write("height", cone.height);
}


void StdSceneWriter::Impl::writeCapsule(Mapping* archive, const SgMesh::Capsule& capsule)
{
    archive->write("type", "Cone");
    archive->write("radius", capsule.radius);
    archive->write("height", capsule.height);
}


MappingPtr StdSceneWriter::Impl::writeAppearance(SgShape* shape)
{
    MappingPtr archive = new Mapping;

    if(auto material = writeMaterial(shape->material())){
        archive->insert("material", material);
    }
    if(archive->empty()){
        archive = nullptr;
    }

    return archive;
}


MappingPtr StdSceneWriter::Impl::writeMaterial(SgMaterial* material)
{
    if(!material){
        return nullptr;
    }
    if(!defaultMaterial){
        defaultMaterial = new SgMaterial;
    }
    MappingPtr archive = new Mapping;

    if(material->diffuseColor() != defaultMaterial->diffuseColor()){
        write(*archive, "diffuse", material->diffuseColor());
    }
    if(material->emissiveColor() != defaultMaterial->emissiveColor()){
        write(*archive, "emissive", material->emissiveColor());
    }
    if(material->specularColor() != defaultMaterial->specularColor()){
        write(*archive, "specular", material->specularColor());
    }
    if(material->shininess() != defaultMaterial->shininess()){
        archive->write("shininess", material->shininess());
    }
    if(material->transparency() != defaultMaterial->transparency()){
        archive->write("transparency", material->transparency());
    }

    if(archive->empty()){
        archive = nullptr;
    }
    
    return archive;
}

#include "widgets/glorpviewer.h"
#include "protocol/dscrtypes.h"

#include <array>
#include <algorithm>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QFile>
#include <QQuaternion>

// Note: Qt recommends a particular pattern for initializing and destroying QOpenGLWidgets
// https://doc.qt.io/qt-6/qopenglwidget.html

GlOrPViewer::GlOrPViewer(QWidget *parent) : QOpenGLWidget{parent} {
}

GlOrPViewer::~GlOrPViewer() {
    this->cleanup();
}

static const GLfloat UNIT_CUBE_GL_VERTICES[24 * 6] = {
    // +X
    1, -1, -1, 1, 0, 0,
    1, 1, -1, 1, 0, 0,
    1, 1, 1, 1, 0, 0,
    1, -1, 1, 1, 0, 0,
    // -X
    -1, -1, -1, -1, 0, 0,
    -1, -1, 1, -1, 0, 0,
    -1, 1, 1, -1, 0, 0,
    -1, 1, -1, -1, 0, 0,
    // +Y
    -1, 1, -1, 0, 1, 0,
    -1, 1, 1, 0, 1, 0,
    1, 1, 1, 0, 1, 0,
    1, 1, -1, 0, 1, 0,
    // -Y
    -1, -1, -1, 0, -1, 0,
    1, -1, -1, 0, -1, 0,
    1, -1, 1, 0, -1, 0,
    -1, -1, 1, 0, -1, 0,
    // +Z
    -1, -1, 1, 0, 0, 1,
    1, -1, 1, 0, 0, 1,
    1, 1, 1, 0, 0, 1,
    -1, 1, 1, 0, 0, 1,
    // -Z
    -1, -1, -1, 0, 0, -1,
    -1, 1, -1, 0, 0, -1,
    1, 1, -1, 0, 0, -1,
    1, -1, -1, 0, 0, -1,
};

static const GLuint UNIT_CUBE_GL_INDICES[36] = {
    // +X
    0,  1,  2,
    0,  2,  3,
    // -X
    4,  5,  6,
    4,  6,  7,
    // +Y
    8,  9, 10,
    8, 10, 11,
    // -Y
    12, 13, 14,
    12, 14, 15,
    // +Z
    16, 17, 18,
    16, 18, 19,
    // -Z
    20, 21, 22,
    20, 22, 23,
};;

static GLubyte lerp(GLubyte start, GLubyte end, float mix) {
    return std::lround(float(start) * (1.0f - mix) + float(end) * mix);
}

// 0-64 decode based off DSVE's colour table
// https://github.com/akqqa/deep-space-visual-editor/blob/69f12bc6033c01d975981e40cbeec890feeee853/src/js/editor.js#L46-L65
// 65-128 decode based off conversation with 1574
// 224  1574
// COLOUR 65 ETC 128 ; ;
// TE-PERSON EYE $ red , green , blue SENSE CAN ! ; ;
// COLOUR word-order ( red , green , blue ) ; ;
// COLOUR ( X = 0 XOR X > 0 AND X < 1 XOR X = 1 ) ; ;
// SMALL ( X0 ) => X0 ( NONE . ) TABL 0.5, 0 ; ;
// 1, 1 ; ;
// 0.2, 0 ; ;
// 4.3, 4 ; ;
// 8.9, 8 ; ;
// COLOUR ( X0 ) TABL ; ;
// IF X0 > 64 AND X0 < 129 => COLOUR ( SMALL ( X0 ; 16 / 4 rem[div] ) 3 / , SMALL ( X0 ; 4 / 4 rem[div] ) 3 / , X0 ; 4 rem[div] 3 / )
// and a correction to the unpacking description
// COLOUR ( X0 ) TABL ; ;
// IF X0 > 64 AND X0 < 129 => COLOUR ( SMALL ( X0 ; 65 - 16 /) 4 rem[div] 3 / , SMALL ( X0 ; 65 - 4 /) 4 rem[div] 3 / , X0 ; 65 - 4 rem[div] 3 / )
static std::array<GLubyte, 3> getIndexedColour(int index) {
    const GLubyte R[] = { 0xFF, 0xBB, 0x00, 0x00, 0x4D, 0xFB, 0xFF, 0x48, 0x63, 0xFF };
    const GLubyte G[] = { 0x58, 0xFF, 0xCD, 0x84, 0x00, 0x39, 0x0F, 0x48, 0x63, 0xFF };
    const GLubyte B[] = { 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xD7, 0x48, 0x63, 0xFF };

    if(index >= 65 && index <= 128) {
        // Unpack modulo 4 and scale to [0.0, 1.0]: c((x0 - 65) / 16 % 4 / 3, (x0 - 65) / 4 % 4 / 3, (x0 - 65) % 4 / 3)
        // Scale to GLubyte range: 255 / 3 == 85
        int x0_m65 = index - 65;
        GLubyte r = ((x0_m65 >> 4) & 0b11) * 85;
        GLubyte g = ((x0_m65 >> 2) & 0b11) * 85;
        GLubyte b = ((x0_m65 >> 0) & 0b11) * 85;
        return { r, g, b };
    } else {
        float scaledIdx = float(std::clamp(index, 0, 64)) / 64.0f * (sizeof(R) - 1);
        int loIdx = std::floor(scaledIdx);
        int hiIdx = std::ceil(scaledIdx);
        float mix = scaledIdx - loIdx;

        GLubyte r = lerp(R[loIdx], R[hiIdx], mix);
        GLubyte g = lerp(G[loIdx], G[hiIdx], mix);
        GLubyte b = lerp(B[loIdx], B[hiIdx], mix);
        return { r, g, b };
    }
}

bool GlOrPViewer::load53(const DscrSpecialPlot &plot) {
    this->m_boundingSphereRadius = 0.0f;
    this->m_spheres.drawInstances.clear();
    for(auto &orb : plot.orbs) {
        QMatrix4x4 model;
        DrawInstance oai;

        model.translate(orb.x, orb.y, orb.z);
        float orbRadius = orb.d / 2.0f;
        model.scale(orbRadius);
        std::copy_n(model.constData(), 16, oai.m);
        auto colour = getIndexedColour(orb.colourIdx);
        oai.rgba[0] = colour[0];
        oai.rgba[1] = colour[1];
        oai.rgba[2] = colour[2];
        oai.rgba[3] = 255;

        float modelOuterExtremity = QVector3D(orb.x, orb.y, orb.z).length() + orbRadius;
        if(modelOuterExtremity > this->m_boundingSphereRadius) {
            this->m_boundingSphereRadius = modelOuterExtremity;
        }

        this->m_spheres.drawInstances.append(oai);
    }
    this->m_cubes.drawInstances.clear();
    for(auto &box : plot.boxes) {
        QMatrix4x4 model;
        DrawInstance oai;

        model.translate(box.x, box.y, box.z);
        model.scale(box.xn / 2.0f, box.yn / 2.0f, box.zn / 2.0f);
        std::copy_n(model.constData(), 16, oai.m);
        auto colour = getIndexedColour(box.colourIdx);
        oai.rgba[0] = colour[0];
        oai.rgba[1] = colour[1];
        oai.rgba[2] = colour[2];
        oai.rgba[3] = 255;

        float modelOuterExtremity = QVector3D(box.x + box.xn / 2.0f, box.y + box.yn / 2.0f, box.z + box.zn / 2.0f).length();
        if(modelOuterExtremity > this->m_boundingSphereRadius) {
            this->m_boundingSphereRadius = modelOuterExtremity;
        }

        this->m_cubes.drawInstances.append(oai);
    }
    for(auto &seg: plot.segs) {
        // hacky line by stretching a box
        QVector3D p0(seg.x0, seg.y0, seg.z0);
        QVector3D p1(seg.x1, seg.y1, seg.z1);
        QVector3D v = p1 - p0;

        QMatrix4x4 model;
        DrawInstance oai;

        model.translate((p0 + p1) / 2);
        model.rotate(QQuaternion::rotationTo(QVector3D(1.0f, 0.0f, 0.0f), v));
        model.scale(v.length() / 2.0f, 0.02f, 0.02f);
        std::copy_n(model.constData(), 16, oai.m);
        auto colour = getIndexedColour(seg.colourIdx);
        oai.rgba[0] = colour[0];
        oai.rgba[1] = colour[1];
        oai.rgba[2] = colour[2];
        oai.rgba[3] = 255;

        float modelOuterExtremity = std::max(p0.length(), p1.length());
        if(modelOuterExtremity > this->m_boundingSphereRadius) {
            this->m_boundingSphereRadius = modelOuterExtremity;
        }

        this->m_cubes.drawInstances.append(oai);
    }
    this->m_drawInstancesDirty = true;

    // caller should call this->update() when ready to present
    return true;
}

static void initializeMeshInstanceCollection(MeshInstanceCollection& mic, QOpenGLShaderProgram *m_program, const void *vertexData, int vertexBytes, const void *indexData, int indexBytes, int indexSize) {
    QOpenGLExtraFunctions *g = QOpenGLContext::currentContext()->extraFunctions();

    mic.vao.create();
    if (mic.vao.isCreated()) {
        mic.vao.bind();
    }

    mic.vbo.create();
    mic.vbo.bind();
    mic.vbo.allocate(vertexData, vertexBytes);
    const int STRIDE = 6 * sizeof(GLfloat);
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, STRIDE);
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(GLfloat), 3, STRIDE);
    mic.vbo.release();

    mic.ibo.create();
    mic.ibo.bind();
    mic.ibo.allocate(indexData, indexBytes);
    mic.numIboIndices = indexSize;

    mic.ivbo.create();
    mic.ivbo.bind();
    mic.numArrayInstances = 0;
    for(int i = 0; i < 4; i++) { // 2-5
        m_program->enableAttributeArray(2 + i);
        m_program->setAttributeBuffer(2 + i, GL_FLOAT, i * 4 * sizeof(GLfloat), 4, sizeof(DrawInstance));
        g->glVertexAttribDivisor(2 + i, 1);
    }
    m_program->enableAttributeArray(6);
    m_program->setAttributeBuffer(6, GL_UNSIGNED_BYTE, 4 * 4 * sizeof(GLfloat), 4, sizeof(DrawInstance));
    g->glVertexAttribDivisor(6, 1);
    mic.ivbo.release();

    mic.vao.release();
}

void GlOrPViewer::initializeGL() {
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();

    f->glEnable(GL_DEPTH_TEST);
    f->glEnable(GL_CULL_FACE);

    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/blinn_phong_vertex.glsl");
    m_program->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/blinn_phong_frag.glsl");
    m_program->link();

    QVector<GLfloat> vertices;
    QVector<GLuint> indices;

    // Parser hardcoded specifically for reading a Blender-exported obj file with no normal or UV data
    QFile icoSphFile(":/meshes/icosphere4.obj");
    if(icoSphFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while(!icoSphFile.atEnd()) {
            auto line = icoSphFile.readLine().simplified().split(' ');
            if(line[0] == "v") {
                // stride = 24, offset = 0
                vertices.append(line[1].toFloat());
                vertices.append(line[2].toFloat());
                vertices.append(line[3].toFloat());
                // offset = 3
                // For the case of a unit icosphere, normal is equal to position
                vertices.append(line[1].toFloat());
                vertices.append(line[2].toFloat());
                vertices.append(line[3].toFloat());
            } else if(line[0] == "f") {
                indices.append(line[1].toInt() - 1);
                indices.append(line[2].toInt() - 1);
                indices.append(line[3].toInt() - 1);
            }
        }
        icoSphFile.close();
    }

    initializeMeshInstanceCollection(this->m_spheres, m_program, vertices.data(), vertices.size() * sizeof(GLfloat), indices.data(), indices.size() * sizeof(GLuint), indices.size());
    initializeMeshInstanceCollection(this->m_cubes, m_program, UNIT_CUBE_GL_VERTICES, sizeof(UNIT_CUBE_GL_VERTICES), UNIT_CUBE_GL_INDICES, sizeof(UNIT_CUBE_GL_INDICES), sizeof(UNIT_CUBE_GL_INDICES) / sizeof(GLuint));

    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &GlOrPViewer::cleanup);
}

void GlOrPViewer::resizeGL(int w, int h) {
    this->m_currentDrawboxAspectRatio = w / float(h);
}

void GlOrPViewer::paintGL() {
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    QOpenGLExtraFunctions *g = QOpenGLContext::currentContext()->extraFunctions();
    f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // TODO should probably compute an offset bounding sphere and translate camera instead of fixing the
    // centre of the sphere at (0, 0, 0) for views that don't support zoom/rotation control (e.g. instantGlorp)
    float cameraVerticalAngle = M_PI / 4.0f; // 45 deg
    float cameraVerticalAngleHalf = cameraVerticalAngle / 2.0f;
    float cameraHorizAngleHalf = std::atan(std::tan(cameraVerticalAngleHalf) * this->m_currentDrawboxAspectRatio);
    float cameraZDistance = this->m_boundingSphereRadius / std::sin(std::min(cameraVerticalAngleHalf, cameraHorizAngleHalf));
    QMatrix4x4 projection;
    projection.perspective(
        qRadiansToDegrees(cameraVerticalAngle), // bleh
        this->m_currentDrawboxAspectRatio,
        cameraZDistance - this->m_boundingSphereRadius,
        cameraZDistance + this->m_boundingSphereRadius
    );

    QVector3D cameraPos(0.0f, 0.0f, cameraZDistance);
    QVector3D targetPos(0.0f, 0.0f, 0.0f);
    QVector3D cameraUp(0.0f, 1.0f, 0.0f);
    QMatrix4x4 view;
    view.lookAt(cameraPos, targetPos, cameraUp);

    this->m_program->bind();
    this->m_program->setUniformValue("vp", projection * view);
    this->m_program->setUniformValue("viewPos", cameraPos);
    this->m_program->setUniformValue("lightPos", cameraPos + this->m_boundingSphereRadius * QVector3D(-1.0f, 1.0f, 0.0f));

    if(this->m_drawInstancesDirty) {
        this->m_spheres.ivbo.bind();
        this->m_spheres.ivbo.allocate(this->m_spheres.drawInstances.data(), this->m_spheres.drawInstances.size() * sizeof(DrawInstance));
        this->m_spheres.ivbo.release();
        this->m_spheres.numArrayInstances = this->m_spheres.drawInstances.size();
        this->m_cubes.ivbo.bind();
        this->m_cubes.ivbo.allocate(this->m_cubes.drawInstances.data(), this->m_cubes.drawInstances.size() * sizeof(DrawInstance));
        this->m_cubes.ivbo.release();
        this->m_cubes.numArrayInstances = this->m_cubes.drawInstances.size();
        this->m_drawInstancesDirty = false;
    }

    this->m_spheres.vao.bind();
    g->glDrawElementsInstanced(GL_TRIANGLES, this->m_spheres.numIboIndices, GL_UNSIGNED_INT, nullptr, this->m_spheres.numArrayInstances);
    this->m_spheres.vao.release();

    this->m_cubes.vao.bind();
    g->glDrawElementsInstanced(GL_TRIANGLES, this->m_cubes.numIboIndices, GL_UNSIGNED_INT, nullptr, this->m_cubes.numArrayInstances);
    this->m_cubes.vao.release();

    this->m_program->release();
}

void GlOrPViewer::cleanup() {
    this->makeCurrent();

    delete this->m_program;
    this->m_program = nullptr;

    this->m_cubes.ivbo.destroy();
    this->m_cubes.ibo.destroy();
    this->m_cubes.vbo.destroy();
    this->m_cubes.vao.destroy();

    this->m_spheres.ivbo.destroy();
    this->m_spheres.ibo.destroy();
    this->m_spheres.vbo.destroy();
    this->m_spheres.vao.destroy();

    this->doneCurrent();
    disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &GlOrPViewer::cleanup);
}

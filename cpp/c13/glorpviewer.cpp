#include "glorpviewer.h"
#include "dscrtypes.h"

#include <array>
#include <algorithm>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLExtraFunctions>
#include <QFile>

// Note: Qt recommends a particular pattern for initializing and destroying QOpenGLWidgets
// https://doc.qt.io/qt-6/qopenglwidget.html

GlOrPViewer::GlOrPViewer(QWidget *parent) : QOpenGLWidget{parent} {
}

GlOrPViewer::~GlOrPViewer() {
    this->cleanup();
}

static GLubyte lerp(GLubyte start, GLubyte end, float mix) {
    return std::lround(float(start) * (1.0f - mix) + float(end) * mix);
}

// Based off DSVE's colour table
// https://github.com/akqqa/deep-space-visual-editor/blob/69f12bc6033c01d975981e40cbeec890feeee853/src/js/editor.js#L46-L65
static std::array<GLubyte, 3> getIndexedColour(int index) {
    const GLubyte R[] = { 0xFF, 0xBB, 0x00, 0x00, 0x4D, 0xFB, 0xFF, 0x48, 0x63, 0xFF };
    const GLubyte G[] = { 0x58, 0xFF, 0xCD, 0x84, 0x00, 0x39, 0x0F, 0x48, 0x63, 0xFF };
    const GLubyte B[] = { 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xD7, 0x48, 0x63, 0xFF };

    float scaledIdx = float(std::clamp(index, 0, 64)) / 64.0f * (sizeof(R) - 1);
    int loIdx = std::floor(scaledIdx);
    int hiIdx = std::ceil(scaledIdx);
    float mix = scaledIdx - loIdx;

    GLubyte r = lerp(R[loIdx], R[hiIdx], mix);
    GLubyte g = lerp(G[loIdx], G[hiIdx], mix);
    GLubyte b = lerp(B[loIdx], B[hiIdx], mix);

    return { r, g, b };
}

bool GlOrPViewer::load53(const DscrSpecialPlot &orbs) {
    this->m_drawInstances.clear();
    for(auto &orb : orbs.orbs) {
        QMatrix4x4 model;
        DrawInstance oai;

        model.translate(orb.x, orb.y, orb.z);
        model.scale(orb.d / 2.0f);
        std::copy_n(model.constData(), 16, oai.m);
        auto colour = getIndexedColour(orb.colourIdx);
        oai.rgba[0] = colour[0];
        oai.rgba[1] = colour[1];
        oai.rgba[2] = colour[2];
        oai.rgba[3] = 255;

        this->m_drawInstances.append(oai);
    }
    this->m_drawInstancesDirty = true;

    // caller should call this->update() when ready to present
    return true;
}

void GlOrPViewer::initializeGL() {
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    QOpenGLExtraFunctions *g = QOpenGLContext::currentContext()->extraFunctions();

    f->glEnable(GL_DEPTH_TEST);
    f->glEnable(GL_CULL_FACE);

    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/blinn_phong_vertex.glsl");
    m_program->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/blinn_phong_frag.glsl");
    m_program->link();

    m_vao.create();
    if (m_vao.isCreated()) {
        m_vao.bind();
    }

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

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(vertices.data(), vertices.size() * sizeof(GLfloat));
    const int STRIDE = 6 * sizeof(GLfloat);
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, STRIDE);
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(GLfloat), 3, STRIDE);
    m_vbo.release();

    this->m_ibo.create();
    this->m_ibo.bind();
    this->m_ibo.allocate(indices.data(), indices.size() * sizeof(GLuint));
    this->m_numIboIndices = indices.size();

    this->m_ivbo.create();
    this->m_ivbo.bind();
    this->m_numArrayInstances = 0;
    for(int i = 0; i < 4; i++) { // 2-5
        m_program->enableAttributeArray(2 + i);
        m_program->setAttributeBuffer(2 + i, GL_FLOAT, i * 4 * sizeof(GLfloat), 4, sizeof(DrawInstance));
        g->glVertexAttribDivisor(2 + i, 1);
    }
    m_program->enableAttributeArray(6);
    m_program->setAttributeBuffer(6, GL_UNSIGNED_BYTE, 4 * 4 * sizeof(GLfloat), 4, sizeof(DrawInstance));
    g->glVertexAttribDivisor(6, 1);
    this->m_ivbo.release();

    m_vao.release();

    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &GlOrPViewer::cleanup);
}

void GlOrPViewer::resizeGL(int w, int h) {
    m_projection.setToIdentity();
    m_projection.perspective(45.0f, w / float(h), 0.01f, 100.0f);
}

void GlOrPViewer::paintGL() {
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    QOpenGLExtraFunctions *g = QOpenGLContext::currentContext()->extraFunctions();
    f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    QVector3D cameraPos(0.0f, 0.0f, 50.0f);
    QVector3D targetPos(0.0f, 0.0f, 0.0f);
    QVector3D cameraUp(0.0f, 1.0f, 0.0f);
    QMatrix4x4 view;
    view.lookAt(cameraPos, targetPos, cameraUp);

    this->m_program->bind();
    this->m_program->setUniformValue("vp", m_projection * view);
    this->m_program->setUniformValue("viewPos", cameraPos);
    this->m_program->setUniformValue("lightPos", cameraPos + QVector3D(-2.0f, 2.0f, 0.0f));

    if(this->m_drawInstancesDirty) {
        this->m_ivbo.bind();
        this->m_ivbo.allocate(this->m_drawInstances.data(), this->m_drawInstances.size() * sizeof(DrawInstance));
        this->m_ivbo.release();
        this->m_numArrayInstances = this->m_drawInstances.size();
        this->m_drawInstancesDirty = false;
    }

    this->m_vao.bind();
    g->glDrawElementsInstanced(GL_TRIANGLES, this->m_numIboIndices, GL_UNSIGNED_INT, nullptr, this->m_numArrayInstances);
    this->m_vao.release();
    this->m_program->release();
}

void GlOrPViewer::cleanup() {
    this->makeCurrent();

    delete this->m_program;
    this->m_program = nullptr;

    this->m_ivbo.destroy();
    this->m_ibo.destroy();
    this->m_vbo.destroy();
    this->m_vao.destroy();

    this->doneCurrent();
    disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &GlOrPViewer::cleanup);
}

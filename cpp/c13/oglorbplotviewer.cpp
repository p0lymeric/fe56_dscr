#include "oglorbplotviewer.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QFile>

// Note: Qt recommends a particular pattern for initializing and destroying QOpenGLWidgets
// https://doc.qt.io/qt-6/qopenglwidget.html

OGlOrbPlotViewer::OGlOrbPlotViewer(QWidget *parent) : QOpenGLWidget{parent} {
}

OGlOrbPlotViewer::~OGlOrbPlotViewer() {
    this->cleanup();
}

void OGlOrbPlotViewer::initializeGL() {
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();

    f->glEnable(GL_DEPTH_TEST);
    f->glEnable(GL_CULL_FACE);

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
    }

    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(vertices.data(), vertices.size() * sizeof(GLfloat));

    this->m_ibo.create();
    this->m_ibo.bind();
    this->m_ibo.allocate(indices.data(), indices.size() * sizeof(GLuint));
    this->m_numIboIndices = indices.size();

    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shaders/blinn_phong_vertex.glsl");
    m_program->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shaders/blinn_phong_frag.glsl");
    m_program->link();

    const int STRIDE = 6 * sizeof(GLfloat);
    m_program->enableAttributeArray(0);
    m_program->setAttributeBuffer(0, GL_FLOAT, 0, 3, STRIDE);
    m_program->enableAttributeArray(1);
    m_program->setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(GLfloat), 3, STRIDE);

    m_vao.release();

    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &OGlOrbPlotViewer::cleanup);
}

void OGlOrbPlotViewer::resizeGL(int w, int h) {
    m_projection.setToIdentity();
    m_projection.perspective(45.0f, w / float(h), 0.01f, 100.0f);
}

void OGlOrbPlotViewer::paintGL() {
    QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
    f->glClearColor(0.5f, 0.0f, 0.0f, 1.0f);
    f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    QVector3D cameraPos(0.0f, 0.0f, 5.0f);
    QVector3D targetPos(0.0f, 0.0f, 0.0f);
    QVector3D cameraUp(0.0f, 1.0f, 0.0f);
    QMatrix4x4 view;
    view.lookAt(cameraPos, targetPos, cameraUp);

    this->m_program->bind();
    this->m_program->setUniformValue("vp", m_projection * view);
    this->m_program->setUniformValue("viewPos", cameraPos);
    this->m_program->setUniformValue("lightPos", cameraPos + QVector3D(-2.0f, 2.0f, 0.0f));

    this->m_vao.bind();
    f->glDrawElements(GL_TRIANGLES, this->m_numIboIndices, GL_UNSIGNED_INT, nullptr);
    this->m_vao.release();
    this->m_program->release();
}

void OGlOrbPlotViewer::cleanup() {
    this->makeCurrent();

    delete this->m_program;
    this->m_program = nullptr;

    this->m_ibo.destroy();
    this->m_vbo.destroy();
    this->m_vao.destroy();

    this->doneCurrent();
    disconnect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &OGlOrbPlotViewer::cleanup);
}

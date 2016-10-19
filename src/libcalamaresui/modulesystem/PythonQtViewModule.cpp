/* === This file is part of Calamares - <http://github.com/calamares> ===
 *
 *   Copyright 2016, Teo Mrnjavac <teo@kde.org>
 *
 *   Calamares is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Calamares is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Calamares. If not, see <http://www.gnu.org/licenses/>.
 */

#include "PythonQtViewModule.h"

#include "utils/Logger.h"
#include "viewpages/ViewStep.h"
#include "viewpages/PythonQtViewStep.h"
#include "ViewManager.h"
#include "CalamaresConfig.h"
#include "GlobalStorage.h"
#include "JobQueue.h"

#include <PythonQt.h>
#include <extensions/PythonQt_QtAll/PythonQt_QtAll.h>

#include <QDir>
#include <QPointer>


/**
 * @brief This GlobalStorage class is a namespace-free wrapper for
 *        Calamares::GlobalStorage. This is unfortunately a necessity
 *        because PythonQt doesn't like namespaces.
 */
class GlobalStorage : public QObject
{
    Q_OBJECT
public:
    explicit GlobalStorage( Calamares::GlobalStorage* gs )
        : QObject( gs )
        , m_gs( gs )
    {}

public slots:
    bool contains( const QString& key ) const
    { return m_gs->contains( key ); }

    int count() const
    { return m_gs->count(); }

    void insert( const QString& key, const QVariant& value )
    { m_gs->insert( key, value ); }

    QStringList keys() const
    { return m_gs->keys(); }

    int remove( const QString& key )
    { return m_gs->remove( key ); }

    QVariant value( const QString& key ) const
    { return m_gs->value( key ); }
private:
    Calamares::GlobalStorage* m_gs;
};

static QPointer< GlobalStorage > s_gs = nullptr;


namespace Calamares {


Module::Type
PythonQtViewModule::type() const
{
    return View;
}


Module::Interface
PythonQtViewModule::interface() const
{
    return PythonQtInterface;
}


void
PythonQtViewModule::loadSelf()
{
    if ( !m_scriptFileName.isEmpty() )
    {
        if ( PythonQt::self() == nullptr )
        {
            if ( Py_IsInitialized() )
                PythonQt::init( PythonQt::IgnoreSiteModule |
                                PythonQt::RedirectStdOut |
                                PythonQt::PythonAlreadyInitialized );
            else
                PythonQt::init();

            PythonQt_QtAll::init();
            cDebug() << "Initializing PythonQt bindings."
                     << "This should only happen once.";

            //TODO: register classes here into the PythonQt environment, like this:
            //PythonQt::self()->registerClass( &PythonQtViewStep::staticMetaObject,
            //                                 "calamares" );

            // Prepare GlobalStorage object, in module PythonQt.calamares
            if ( !s_gs )
                s_gs = new ::GlobalStorage( Calamares::JobQueue::instance()->globalStorage() );

            PythonQt::self()->registerClass( &::GlobalStorage::staticMetaObject,
                                             "calamares" );
            PythonQtObjectPtr pqtm = PythonQt::priv()->pythonQtModule();
            PythonQtObjectPtr cala = PythonQt::self()->lookupObject( pqtm, "calamares" );

            cala.addObject( "global_storage", s_gs );

            // Basic stdout/stderr handling
            QObject::connect( PythonQt::self(), &PythonQt::pythonStdOut,
                     []( const QString& message )
            {
                cDebug() << "PythonQt OUT>" << message;
            } );
            QObject::connect( PythonQt::self(), &PythonQt::pythonStdErr,
                     []( const QString& message )
            {
                cDebug() << "PythonQt ERR>" << message;
            } );

        }

        QDir workingDir( m_workingPath );
        if ( !workingDir.exists() )
        {
            cDebug() << "Invalid working directory"
                     << m_workingPath
                     << "for module"
                     << name();
            return;
        }

        QString fullPath = workingDir.absoluteFilePath( m_scriptFileName );
        QFileInfo scriptFileInfo( fullPath );
        if ( !scriptFileInfo.isReadable() )
        {
            cDebug() << "Invalid main script file path"
                     << fullPath
                     << "for module"
                     << name();
            return;
        }

        // Construct empty Python module with the given name
        PythonQtObjectPtr cxt =
                PythonQt::self()->
                createModuleFromScript( name() );
        if ( cxt.isNull() )
        {
            cDebug() << "Cannot load PythonQt context from file"
                     << fullPath
                     << "for module"
                     << name();
            return;
        }

        QString calamares_module_annotation =
                "_calamares_module_typename = 'foo'\n"
                "def calamares_module(viewmodule_type):\n"
                "    global _calamares_module_typename\n"
                "    _calamares_module_typename = viewmodule_type.__name__\n"
                "    return viewmodule_type\n";

        // Load in the decorator
        PythonQt::self()->evalScript( cxt, calamares_module_annotation );

        // Load the module
        PythonQt::self()->evalFile( cxt, fullPath );

        m_viewStep = new PythonQtViewStep( cxt );

        cDebug() << "PythonQtViewModule loading self for instance" << instanceKey()
                 << "\nPythonQtViewModule at address" << this
                 << "\nViewStep at address" << m_viewStep;

        m_viewStep->setModuleInstanceKey( instanceKey() );
        m_viewStep->setConfigurationMap( m_configurationMap );
        ViewManager::instance()->addViewStep( m_viewStep );
        m_loaded = true;
        cDebug() << "PythonQtViewModule" << instanceKey() << "loading complete.";
    }
}


QList< job_ptr >
PythonQtViewModule::jobs() const
{
    return m_viewStep->jobs();
}


void
PythonQtViewModule::initFrom( const QVariantMap& moduleDescriptor )
{
    Module::initFrom( moduleDescriptor );
    QDir directory( location() );
    m_workingPath = directory.absolutePath();

    if ( !moduleDescriptor.value( "script" ).toString().isEmpty() )
    {
        m_scriptFileName = moduleDescriptor.value( "script" ).toString();
    }
}

PythonQtViewModule::PythonQtViewModule()
    : Module()
{
}

PythonQtViewModule::~PythonQtViewModule()
{
}

} // namespace Calamares

#include "PythonQtViewModule.moc"
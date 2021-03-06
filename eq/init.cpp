
/* Copyright (c) 2005-2017, Stefan Eilemann <eile@equalizergraphics.com>
 *                          Cedric Stalder <cedric.stalder@gmail.com>
 *                          Daniel Nachbaur <danielnachbaur@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 2.1 as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "init.h"

#ifdef EQUALIZER_USE_QT5WIDGETS
#include "qt/windowSystem.h"
#include <QApplication>
#endif

#include "client.h"
#include "config.h"
#include "global.h"
#include "nodeFactory.h"
#include "os.h"
#include "server.h"
#ifdef AGL
#include "agl/windowSystem.h"
#endif
#ifdef GLX
#include "glx/windowSystem.h"
#endif
#ifdef WGL
#include "wgl/windowSystem.h"
#endif

#include <co/global.h>
#include <eq/fabric/configParams.h>
#include <eq/fabric/init.h>
#include <eq/version.h>
#include <lunchbox/file.h>
#include <lunchbox/term.h>
#include <pression/pluginRegistry.h>

#ifdef _WIN32
#pragma warning(push)
#pragma warning(disable : 4275 4251)
#endif
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#ifdef _WIN32
#pragma warning(pop)
#endif

#include <fstream>

#ifdef _MSC_VER
#define atoll _atoi64
#endif
#ifndef MAXPATHLEN
#define MAXPATHLEN 1024
#endif

namespace arg = boost::program_options;

namespace eq
{
namespace
{
static lunchbox::a_int32_t _initialized;
}

const char EQ_HELP[] = "eq-help";
const char EQ_LOGFILE[] = "eq-logfile";
const char EQ_SERVER[] = "eq-server";
const char EQ_CLIENT[] = "eq-client";
const char EQ_CONFIG[] = "eq-config";
const char EQ_CONFIG_FLAGS[] = "eq-config-flags";
const char EQ_CONFIG_PREFIXES[] = "eq-config-prefixes";
const char EQ_RENDER_CLIENT[] = "eq-render-client";

static bool _parseArguments(const int argc, char** argv);
static void _initPlugins();

bool _init(const int argc, char** argv, NodeFactory* nodeFactory)
{
    const char* env = getenv("EQ_LOG_LEVEL");
    if (env)
        lunchbox::Log::level = lunchbox::Log::getLogLevel(env);

    env = getenv("EQ_LOG_TOPICS");
    if (env)
        lunchbox::Log::topics |= atoll(env);

    lunchbox::Log::instance().setThreadName("Main");

    if (++_initialized > 1) // not first
    {
        LBERROR << "Equalizer client library initialized more than once"
                << std::endl;
        return true;
    }

    if (!_parseArguments(argc, argv))
        return false;
    LBDEBUG << "Equalizer v" << Version::getString() << " initializing"
            << std::endl;

#ifdef AGL
    GetCurrentEventQueue();
    WindowSystem::add(WindowSystemImpl(new agl::WindowSystem));
#endif
#ifdef GLX
    ::XInitThreads();
    WindowSystem::add(WindowSystemImpl(new glx::WindowSystem));
#endif
#ifdef WGL
    WindowSystem::add(WindowSystemImpl(new wgl::WindowSystem));
#endif
#ifdef EQUALIZER_USE_QT5WIDGETS
    if (QApplication::instance())
        WindowSystem::add(WindowSystemImpl(new qt::WindowSystem));
#endif

    LBASSERT(nodeFactory);
    Global::_nodeFactory = nodeFactory;

    const std::string& programName = Global::getProgramName();
    if (programName.empty() && argc > 0)
        Global::setProgramName(argv[0]);

    const std::string& workDir = Global::getWorkDir();
    if (workDir.empty())
        Global::setWorkDir(lunchbox::getWorkDir());

    _initPlugins();
    return fabric::init(argc, argv);
}

bool exit()
{
    if (_initialized <= 0)
    {
        LBERROR << "Equalizer client library not initialized" << std::endl;
        return false;
    }
    if (--_initialized > 0) // not last
        return true;

    WindowSystem::clear();
    Global::_nodeFactory = 0;
    return fabric::exit();
}

namespace
{
using Flags = std::unordered_map<std::string, uint32_t>;

Flags _getConfigFlags()
{
    Flags configFlags;
    configFlags["multiprocess"] = fabric::ConfigParams::FLAG_MULTIPROCESS;
    configFlags["multiprocess_db"] = fabric::ConfigParams::FLAG_MULTIPROCESS_DB;
    configFlags["ethernet"] = fabric::ConfigParams::FLAG_NETWORK_ETHERNET;
    configFlags["infiniband"] = fabric::ConfigParams::FLAG_NETWORK_INFINIBAND;
    configFlags["2D_horizontal"] =
        fabric::ConfigParams::FLAG_LOAD_EQ_HORIZONTAL;
    configFlags["2D_vertical"] = fabric::ConfigParams::FLAG_LOAD_EQ_VERTICAL;
    configFlags["2D_tiles"] = fabric::ConfigParams::FLAG_LOAD_EQ_2D;
    return configFlags;
}

arg::options_description _getProgramOptions()
{
    const Flags configFlags = _getConfigFlags();
    arg::options_description options("Equalizer library options",
                                     lunchbox::term::getSize().first);
    options.add_options()(EQ_HELP, "Display usage information and exit")(
        EQ_LOGFILE, arg::value<std::string>(),
        "Redirect log output to given file")(EQ_SERVER,
                                             arg::value<std::string>(),
                                             "The server address")(
        EQ_CONFIG, arg::value<std::string>(),
        "Configuration filename or autoconfig session name")(
        EQ_CONFIG_FLAGS, arg::value<Strings>()->multitoken(),
        "Autoconfiguration flags")(
        EQ_CONFIG_PREFIXES, arg::value<Strings>()->multitoken(),
        "The network prefix filter(s) in CIDR notation for autoconfig "
        "(white-space separated)")(EQ_RENDER_CLIENT, arg::value<std::string>(),
                                   "The render client executable filename");
    return options;
}
}

std::string getHelp()
{
    std::ostringstream os;
    os << _getProgramOptions();
    return os.str();
}

bool _parseArguments(const int argc, char** argv)
{
    const auto options = _getProgramOptions();
    arg::variables_map vm;
    try
    {
        Strings args;
        for (int i = 0; i < argc; ++i)
        {
            if (strcmp(argv[i], "--") != 0)
                args.push_back(argv[i]);
        }
        arg::store(arg::command_line_parser(args)
                       .options(options)
                       .allow_unregistered()
                       .run(),
                   vm);
        arg::notify(vm);
    }
    catch (const std::exception& e)
    {
        LBERROR << "Error in argument parsing: " << e.what() << std::endl;
        return false;
    }

    if (vm.count(EQ_HELP))
    {
        std::cout << options << std::endl;
        return false;
    }

    if (vm.count(EQ_SERVER))
        Global::setServer(vm[EQ_SERVER].as<std::string>());

    if (vm.count(EQ_CONFIG))
        Global::setConfig(vm[EQ_CONFIG].as<std::string>());

    if (vm.count(EQ_CONFIG_FLAGS))
    {
        const Strings& flagStrings = vm[EQ_CONFIG_FLAGS].as<Strings>();
        const auto configFlags = _getConfigFlags();
        uint32_t flags = Global::getFlags();
        for (StringsCIter i = flagStrings.begin(); i != flagStrings.end(); ++i)
        {
            Flags::const_iterator j = configFlags.find(*i);
            if (j != configFlags.end())
                flags |= j->second;
            else
                LBWARN << "Unknown argument for --eq-config-flags: " << *i
                       << std::endl;
        }
        Global::setFlags(flags);
    }

    if (vm.count(EQ_CONFIG_PREFIXES))
    {
        const Strings& prefixes = vm[EQ_CONFIG_PREFIXES].as<Strings>();
        Global::setPrefixes(prefixes);
    }

    if (vm.count(EQ_CLIENT))
    {
        const std::string& renderClient = vm[EQ_CLIENT].as<std::string>();
        const boost::filesystem::path path(renderClient);

        Global::setProgramName(renderClient);
        Global::setWorkDir(path.parent_path().string());
    }

    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--eq-logfile")
        {
            argv[i][2] = 'l'; // rewrite to --lb-logfile
            argv[i][3] = 'b';
        }
    }

    return true;
}

void _initPlugins()
{
    pression::PluginRegistry& plugins = pression::PluginRegistry::getInstance();

    plugins.loadDirectory(lunchbox::getRootPath() +
                          "/share/Equalizer/plugins"); // install dir
    plugins.loadDirectory("/usr/share/Equalizer/plugins");
    plugins.loadDirectory("/usr/local/share/Equalizer/plugins");
    plugins.loadDirectory(".eqPlugins");
    plugins.loadDirectory("/opt/local/lib"); // MacPorts
    plugins.loadDirectory("/usr/local/lib"); // Homebrew

    const char* home = getenv("HOME");
    if (home)
        plugins.loadDirectory(std::string(home) + "/.eqPlugins");
}

Config* getConfig(const int argc, char** argv)
{
    // 1. initialization of a local client node
    ClientPtr client = new Client;
    if (client->initLocal(argc, argv))
    {
        // 2. connect to server
        ServerPtr server = new Server;
        if (client->connectServer(server))
        {
            // 3. choose configuration
            fabric::ConfigParams configParams;
            Config* config = server->chooseConfig(configParams);
            if (config)
                return config;

            LBERROR << "No matching config on server" << std::endl;

            // -2. disconnect server
            client->disconnectServer(server);
        }
        else
            LBERROR << "Can't open server" << std::endl;

        // -1. exit local client node
        client->exitLocal();
    }
    else
        LBERROR << "Can't init local client node" << std::endl;

    return 0;
}

void releaseConfig(Config* config)
{
    if (!config)
        return;

    ServerPtr server = config->getServer();
    LBASSERT(server.isValid());
    server->releaseConfig(config);

    ClientPtr client = server->getClient();
    LBASSERT(client.isValid());

    client->disconnectServer(server);
    client->exitLocal();

    LBASSERTINFO(client->getRefCount() == 1, client->getRefCount());
    LBASSERTINFO(server->getRefCount() == 1, server->getRefCount());
}
}

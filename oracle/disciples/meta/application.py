# -*- encoding: utf-8 -*-

import sys
import web

from meta import conf
from meta import resources
from meta import database
from meta.session import Session
from meta.session_store import SessionStore

class Application(object):
    """
    Application class wrap web.application.run method
    """
    def __init__(self, meta_host=None, meta_port=None,
                 mongo_host=None, mongo_port=None,
                 port_file=None):
        conf.META_HOST = meta_host
        conf.META_PORT = meta_port
        conf.MONGO_HOST = mongo_host
        conf.MONGO_PORT = mongo_port

        assert(port_file)
        f = open(port_file, 'w')
        f.write('meta_host:' + meta_host + '\n')
        f.write('meta_port:' + str(meta_port) + '\n')
        f.write('mongo_host:' + mongo_host + '\n')
        f.write('mongo_port:' + str(mongo_port) + '\n')
        f.close()


        self.ip = meta_host
        self.port = meta_port

        urls = []
        views = {}

        for resource in resources.ALL:
            id_ = str(id(resource))
            urls.extend([resource.__pattern__, id_])
            print("%s: %s" % (resource.__name__, resource.__pattern__))
            views[id_] = resource

        self.app = web.application(urls, views)
        session = Session(self.app, SessionStore(database.sessions()))
        for cls in views.itervalues():
            cls.__session__ = session

    def run(self):
        """
        Run the web server
        """
        sys.argv[1:] = [self.ip + ':' + str(self.port)]
        self.app.run()

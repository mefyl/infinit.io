#!/usr/bin/env python3

import os
import pymongo
import sys

root = os.path.realpath(os.path.dirname(__file__))
sys.path.append(root + '/../src')
sys.path.append(root + '/../../../mongobox')
sys.path.append(root + '/../../../bottle')

import httplib2
import json
import mongobox

import bottle
import infinit.oracles.meta

class Client:

  class Exception(Exception):

    def __init__(self, status, message):
      self.status = int(status)
      super().__init__(message)

  def __init__(self, meta):
    self.__mongo = mongobox.MongoBox()
    self.__server = bottle.WSGIRefServer(port = 0)
    self.__database = None
    self.__cookies = None
    self.__meta = meta

  def __get_cookies(self, headers):
    cookies = headers.get('set-cookie', None)
    if cookies is not None:
      self.__cookies = cookies

  def __set_cookies(self, headers):
    if self.__cookies is not None:
      headers['Cookie'] = self.__cookies

  def __convert_result(self, url, body, headers, content):
    status = headers['status']
    if status != '200':
      raise Meta.Exception(status, 'status %s on /%s with body %s' %
                           (status, url, body))
    if headers['content-type'] == 'application/json':
      return json.loads(content.decode())
    else:
      return content.decode()

  def post(self, url, body = None):
    h = httplib2.Http()
    uri = "http://localhost:%s/%s" % (self.__meta._Meta__server.port,
                                      url)
    headers = {}
    if body is not None:
      headers['Content-Type'] = 'application/json'
      body = json.dumps(body)
    self.__set_cookies(headers)
    resp, content = h.request(uri,
                              'POST',
                              body = body,
                              headers = headers)
    self.__get_cookies(resp)
    return self.__convert_result(url, body, resp, content)

  def get(self, url):
    h = httplib2.Http()
    headers = {}
    self.__set_cookies(headers)
    uri = "http://localhost:%s/%s" % (self.__meta._Meta__server.port,
                                      url)
    resp, content = h.request(uri,
                              'GET',
                              headers = headers)
    self.__get_cookies(resp)
    return self.__convert_result(url, None, resp, content)


class Meta:

  def __init__(self):
    self.__mongo = mongobox.MongoBox()
    self.__server = bottle.WSGIRefServer(port = 0)
    self.__database = None
    self.__client = None

  def __enter__(self):
    self.__mongo.__enter__()
    client = pymongo.MongoClient(port = self.__mongo.port)
    self.__database = client.meta
    def run():
      try:
        app = infinit.oracles.meta.Meta(
          mongo_port = self.__mongo.port)
        app.catchall = False
        bottle.run(app = app,
                   quiet = True,
                   server = self.__server)
      except KeyboardInterrupt:
        pass
    import threading
    self.__thread = threading.Thread(target = run)
    self.__thread.daemon = True
    self.__thread.start()
    while self.__server.port == 0:
      import time
      time.sleep(.1)
    return self

  def __exit__(self, *args, **kwargs):
    self.__mongo.__exit__(*args, **kwargs)

  def post(self, url, body = None):
    return self.client.post(url, body)

  def get(self, url):
    return self.client.get(url)

  def create_user(self, email, password):
    self.__database.users.insert(
      {
        'email': email,
        'password': password,
      })

  @property
  def database(self):
    return self.__database

  @property
  def client(self):
    if self.__client is None:
      self.__client = Client(self)
    return self.__client

def throws(f):
  try:
    f()
  except:
    pass
  else:
    raise Exception('exception expected')

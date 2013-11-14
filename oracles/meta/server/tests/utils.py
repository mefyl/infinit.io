#!/usr/bin/env python3

import http.cookies
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
from uuid import uuid4, UUID

class HTTPException(Exception):

  def __init__(self, status, url, body):
    self.status = int(status)
    super().__init__('status %s on /%s with body %s' % (status, url, body))

class Client:

  def __init__(self, meta):
    self.__mongo = mongobox.MongoBox()
    self.__server = bottle.WSGIRefServer(port = 0)
    self.__database = None
    self.__cookies = None
    self.__meta = meta

  def __exit__(self, *args, **kwargs):
    self.__mongo.__exit__(*args, **kwargs)

  def __get_cookies(self, headers):
    cookies = headers.get('set-cookie', None)
    if cookies is not None:
      self.__cookies = http.cookies.SimpleCookie(cookies)

  def __set_cookies(self, headers):
    if self.__cookies is not None:
      headers['Cookie'] = self.__cookies.output()

  @property
  def cookie(self):
    return self.__cookies

  def __convert_result(self, url, body, headers, content):
    status = headers['status']
    if status != '200':
      raise HTTPException(status, url, body)
    if headers['content-type'] == 'application/json':
      return json.loads(content.decode())
    elif headers['content-type'] == 'text/plain':
      return content.decode()
    else:
      return content

  def request(self, url, method, body):
    h = httplib2.Http()
    uri = "http://localhost:%s/%s" % (self.__meta._Meta__server.port,
                                      url)
    headers = {}
    if body is not None and isinstance(body, dict):
      headers['Content-Type'] = 'application/json'
      body = json.dumps(body)
    self.__set_cookies(headers)
    resp, content = h.request(uri,
                              method,
                              body = body,
                              headers = headers)
    self.__get_cookies(resp)
    return self.__convert_result(url, body, resp, content)

  def post(self, url, body = None):
    return self.request(url, 'POST', body)

  def get(self, url, body = None):
    return self.request(url, 'GET', body)

  def put(self, url, body = None):
    return self.request(url, 'PUT', body)

  def delete(self, url, body = None):
    return self.request(url, 'DELETE', body)
class Meta:

  def __init__(self, enable_emails = False):
    self.__mongo = mongobox.MongoBox()
    self.__server = bottle.WSGIRefServer(port = 0)
    self.__database = None
    self.__client = None
    self.__enalbe_emails = enable_emails
    self.__meta = None

  def __enter__(self):
    self.__mongo.__enter__()
    client = pymongo.MongoClient(port = self.__mongo.port)
    self.__database = client.meta
    def run():
      try:
        self.__meta = infinit.oracles.meta.Meta(
          mongo_port = self.__mongo.port,
          enable_emails = self.__enalbe_emails)
        self.__meta.catchall = False
        bottle.run(app = self.__meta,
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

  @property
  def mailer(self):
    return self.__meta.mailer

  @mailer.setter
  def mailer(self, mailer):
    assert self.__meta is not None
    self.__meta.mailer = mailer

  @property
  def notifier(self):
    return self.__meta.notifier

  @notifier.setter
  def notifier(self, notifier):
    assert self.__meta is not None
    self.__meta.notifier = notifier

  def __exit__(self, *args, **kwargs):
    self.__mongo.__exit__(*args, **kwargs)

  def post(self, url, body = None):
    return self.client.post(url, body)

  def get(self, url, body = None):
    return self.client.get(url, body)

  def put(self, url, body = None):
    return self.client.put(url, body)

  def delete(self, url, body = None):
    return self.client.delete(url, body)

  def create_user(self,
                  email,
                  fullname = None,
                  password = '0' * 64,
                  activation_code = 'no_activation_code',
                  ):
    res = self.post('user/register',
                    {
                      'email': email,
                      'password': password,
                      'fullname': fullname or email.split('@')[0],
                      'activation_code': activation_code,
                    })
    assert res['success']
    return password

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

class User(Client):

  def __init__(self,
               meta,
               email,
               device_name = 'device',
               **kwargs):
    super().__init__(meta)
    self.email = email
    self.password = meta.create_user(email,
                                     **kwargs)
    self.id = meta.get('user/%s/view' % self.email)['_id']
    self.device_id = uuid4()

  def login(self, device_id = None):
    if device_id is not None:
      self.device_id = device_id

    req = {
      'email': self.email,
      'password': self.password,
      'device_id': str(self.device_id),
    }
    res = self.post('login', req)
    assert res['success']
    assert res['device_id'] == str(self.device_id)

  def logout(self):
    res = self.post('logout', {})
    assert res['success']

  @property
  def device(self):
    assert self.device_id is not None
    return self.get('device/%s/view' % str(self.device_id))

  @property
  def device_name(self):
    return self.device['name']

  @property
  def swaggers(self):
    return self.get('user/swaggers')['swaggers']

  @property
  def favorites(self):
    return self.get('user/self')['favorites']

  @property
  def logged_in(self):
    try:
      res = self.get('user/self')
      assert res['success']
      assert str(self.device_id) in res['devices']
      return True
    except HTTPException as e:
      assert e.status == 403
      return False

  @property
  def devices(self):
    return self.get('devices')['devices']

  @property
  def avatar(self):
    return self.get('user/%s/avatar' % self.id)

  @property
  def connected(self):
    return self.get('user/%s/connected' % self.id)['connected']

  @property
  def identity(self):
    return self.get('user/self')['identity']

  @property
  def connected_on_device(self, device_id = None):
    if device_id is None:
      device_id = self.device_id
    return self.get('device/%s/%s/connected' % (self.id, str(device_id)))['connected']

  def sendfile(self,
               recipient_id,
               files = ['a file with strange encoding: é.file', 'another file with no extension'],
               total_size = 2164062,
               message = 'no message',
               is_directory = False,
               device_id = None,
               ):
    if device_id is None:
      device_id = self.device_id

    transaction = {
      'id_or_email': recipient_id,
      'files': files,
      'files_count': len(files),
      'total_size': total_size,
      'message': message,
      'is_directory': is_directory,
      'device_id': str(device_id),
    }

    res = self.post('transaction/create', transaction)

    return transaction, res
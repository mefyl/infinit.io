#!/usr/bin/env python3

import infinit.oracles.emailer
import infinit.oracles.meta.server
from infinit.oracles.meta.server.mail import Mailer
from infinit.oracles.meta.server.invitation import Invitation
from infinit.oracles.meta.server import transaction_status
from infinit.oracles.meta import version as Version
from infinit.oracles.utils import api
from random import uniform

import datetime
import socket
import http.cookies
import os
import bson
import pymongo
import sys

root = os.path.realpath(os.path.dirname(__file__))
sys.path.append(root + '/../src')
sys.path.append(root + '/../../../mongobox')
sys.path.append(root + '/../../../bottle')

import bottle
import json
import mongobox
import requests

from uuid import uuid4, UUID

class Notification(dict):
  def __init__(self, body):
    super().__init__(body)

  @property
  def type(self):
    return self.notification_type
  def __getattr__(self, key):
    return self[key]
  def __setattr__(self, key, value):
    self[key] = value
  def __lt__(self, other):
    return self.notification_type < other.notification_type
  def __gt__(self, other):
    return self.notification_type > other.notification_type
  def __le__(self, other):
    return self.notification_type <= other.notification_type
  def __ge__(self, other):
    return self.notification_type >= other.notification_type

def next_notification(user):
  assert user.trophonius is not None
  from time import sleep
  user.trophonius.poll()
  return user.notifications.pop(0)

class HTTPException(Exception):

  def __init__(self, status, method, url, body, content):
    self.status = int(status)
    self.content = content
    super().__init__('status %s with %s on /%s with body %s: %s' % \
                     (status, method, url, body, content))
    assert status != 500

class Client:

  def __init__(self, meta):
    self.__cookies = None
    self.__meta_port = meta.port
    self.__session = requests.Session()
    self.user_agent = 'MetaClient/' + Version.version

  @property
  def cookies(self):
    return self.__session.cookies

  def __convert_result(self, url, method, body, response):
    headers = response.headers
    if headers.get('content-type') == 'application/json':
      content = response.json()
    elif headers.get('content-type') == 'text/plain':
      content = response.text
    else:
      content = response.content
    if int(response.status_code) / 100 >= 4:
      raise HTTPException(response.status_code,
                          method, url, body, content)
    else:
      return content

  def request(self, url, method,
              body = None,
              content_type = None,
              content_length = None,
              raw = False):
    uri = "http://127.0.0.1:%s/%s" % (self.__meta_port, url)
    headers = {
      'User-Agent': self.user_agent,
    }
    if body is not None and isinstance(body, dict):
      headers['Content-Type'] = 'application/json'
      body = json.dumps(body)
    else:
      if content_type is not None:
        headers['Content-Type'] = content_type
      if content_length is not None:
        headers['Content-Length'] = content_length
    request = requests.Request(method, uri,
                               headers = headers,
                               data = body,
                               )
    response = self.__session.send(self.__session.prepare_request(request), allow_redirects = False)
    if raw:
      return response
    else:
      return self.__convert_result(url, method, body, response)

  def post(self, url, *args, **kwargs):
    return self.request(url, 'POST', *args, **kwargs)

  def get(self, url, *args, **kwargs):
    return self.request(url, 'GET', *args, **kwargs)

  def put(self, url, *args, **kwargs):
    return self.request(url, 'PUT', *args, **kwargs)

  def delete(self, url, *args, **kwargs):
    return self.request(url, 'DELETE', *args, **kwargs)

class Trophonius(Client):
  class Accepter:

    def poll(self, duration):
      from time import sleep
      sleep(duration)

    def __init__(self, trophonius):
      self.index = 0
      self.trophonius = trophonius
      self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
      self.socket.settimeout(0.2) # seconds.
      self.socket.bind(("localhost", 0))
      self.port = self.socket.getsockname()[1]
      self.socket.listen(2)
      def poll():
        while True:
          try:
            client = self.socket.accept()
            client = client[0]
            representation = str(client.recv(65535), 'utf-8')
            representation = representation[:-1] # remove \n
            d = json.loads(representation)
            # Ignore configuration notification.
            if d['notification']['notification_type'] == 14:
              # OS X requires a larger backlog for the tests to function.
              self.socket.listen(5)
              self.poll()
              continue
            d['notification'].pop('timestamp')
            for user in self.trophonius.users_on_device[UUID(d['device_id'])]:
              user.notifications.append(Notification(d['notification']))
            # OS X requires a larger backlog for the tests to function.
            self.socket.listen(5)
          except BaseException as e:
            continue
      import threading
      self.__thread = threading.Thread(target = poll)
      self.__thread.daemon = True
      self.__thread.start()

  def __init__(self, meta):
    super().__init__(meta)
    self.__uuid = str(uuid4())
    self.__users = {}
    self.__meta = meta
    self.users_on_device = {}
    self.meta_accepter = Trophonius.Accepter(self)
    self.client_accepter = Trophonius.Accepter(self)
    self.__args = {
      'port': self.meta_accepter.port,
      'port_client': 23457,
      'port_client_ssl': 23458,
    }

  def poll(self, duration = 0.1):
    return self.meta_accepter.poll(duration)

  def __enter__(self):
    res = self.put('trophonius/%s' % self.__uuid, self.__args)
    assert res['success']
    return self

  def __exit__(self, *args, **kwargs):
    res = self.delete('trophonius/%s' % self.__uuid)
    assert res['success']

  def connect_user(self, user):
    self.users_on_device.setdefault(user.device_id, set())
    user_id = str(user.id)
    url = 'trophonius/%s/users/%s/%s' % \
          (self.__uuid, user_id, str(user.device_id))
    v = user.version
    body = {}
    if v is not None:
      body['version'] = {
        'major': v[0],
        'minor': v[1],
        'subminor': v[2],
      }
    res = user.put(url, body)
    assert res['success']
    self.__users.setdefault(user_id, [])
    self.__users[user_id].append(user.device_id)
    self.users_on_device[user.device_id].add(user)
    setattr(user, 'trophonius', self)
    setattr(user, 'notifications', [])
    setattr(user, 'next_notification', lambda: next_notification(user))

  def disconnect_user(self, user):
    assert user.device_id in self.__users[user.id]
    res = user.delete('trophonius/%s/users/%s/%s' % \
                      (self.__uuid, str(user.id), str(user.device_id)))
    assert res['success']
    self.__users[user.id].remove(user.device_id)

class NoOpMailer(Mailer):

  def __init__(self, op = None):
    self.__sent = 0
    super().__init__(True)

  def _Mailer__send(self, message):
    self.__sent += 1
    self.view_message(message)
    return [{
      'reject_reason': None,
      '_id': 'b10ce335eb5545a4a8c9381917020f92',
      'status': 'sent',
      'email': 'web@infinit.io',
    }]

  def _Mailer__send_template(self, template_name, message):
    self.__sent += 1
    self.template_message(template_name, message)
    return [{
      'reject_reason': None,
      '_id': 'b10ce335eb5545a4a8c9381917020f92',
      'status': 'sent',
      'email': 'web@infinit.io',
    }]

  def template_message(self, template_name, message):
    self.view_message(message)
    pass

  def view_message(self, message):
    pass

  @property
  def sent(self):
    return self.__sent

class NoOpInvitation(Invitation):

  class FakeMailsnake:

    def __init__(self):
      self.__lists = {}
      self.__unsubscribed = {}

    def userbase(self, name):
      return self.__lists.setdefault(name, set())

    def unsubscribed(self, name):
      return self.__unsubscribed.setdefault(name, set())

    def listSubscribe(self, id, email_address, double_optin):
      if email_address in self.unsubscribed(id):
        self.unsubscribed(id).remove(email_address)
      self.userbase(id).add(email_address)
      return True

    def listUnsubscribe(self, id, email_address):
      try:
        self.userbase(id).remove(email_address)
        self.unsubscribed(id).add(email_address)
        return True
      except KeyError:
        return False

    def listMemberInfo(self, id, email_address):
      if email_address in self.unsubscribed(id):
        return {
          "success": 1,
          "data": [{"status": "unsubscribed"}]
        }
      if email_address in self.userbase(id):
        return {
          "success": 1,
          "data": [{"status": "subscribed"}]
        }
      else:
        return {
          "success": 1,
          "data": []
        }

    def listMembers(self, id):
      return {"data": self.userbase(id)}

  def __init__(self):
    super().__init__(True)
    self.ms = NoOpInvitation.FakeMailsnake()
    # XXX: Reset lists to secure the tests.
    # self.lists = {}


class Email:

  def __str__(self):
    return repr(self)

  def __repr__(self):
    return 'Email(%r, %r, %r)' % \
      (self.template, self.sender.email, self.recipient.email)

class TestEmailer(infinit.oracles.emailer.Emailer):

  def __init__(self):
    self.__emails = []

  def __check (self, o):
    if o.template == 'ghost-invitation':
      assert 'ghost_profile' in o.variables

  def send_one(self,
               template,
               recipient_email,
               recipient_name = None,
               sender_email = None,
               sender_name = None,
               variables = None,
             ):
    o = Email()
    o.template = template
    o.recipient = Email()
    o.recipient.email = recipient_email
    o.recipient.name = recipient_name
    o.sender = Email()
    o.sender.email = sender_email
    o.sender.name = sender_name
    o.variables = variables
    self.__check(o)
    self.__emails.append(o)

  @property
  def emails(self):
    res = self.__emails
    self.__emails = []
    return res


class InstrumentedMeta(infinit.oracles.meta.server.Meta):

  def __init__(self, *args, **kwargs):
    super().__init__(*args, **kwargs)
    self.__now = datetime.datetime.utcnow()
    self.__gcs_buckets = {}

  @property
  def now(self):
    return self.__now

  def forward(self, duration):
    self.__now += duration

  class GCSObject:
    pass

  @api('/gcs/<bucket>/<path:path>', method = 'PUT')
  def gcs_upload_api(self, bucket, path,
                     GoogleAccessId, Expires, Signature):
    bucket = self.__gcs_buckets.setdefault(bucket, {})
    stored = InstrumentedMeta.GCSObject()
    stored.length = bottle.request.headers['Content-Length']
    stored.type = bottle.request.headers['Content-Type']
    stored.data = bottle.request.body
    if path not in bucket:
      bottle.response.status = 204
    bucket[path] = stored

  @api('/gcs/<bucket>', method = 'GET')
  def gcs_upload_api(self, bucket):
    if bucket not in self.__gcs_buckets:
      self.not_found()
    content = ('''\
    <Key>%s</Key>
    <Generation>1432207653322000</Generation>
    <MetaGeneration>1</MetaGeneration>
    <LastModified>2015-05-21T11:27:33.321Z</LastModified>
    <ETag>"3dc6862aaced087142142587cba2123e"</ETag>
    <Size>8</Size>
    <Owner>
      <ID>00b4903a97ebc5d8c3244d92a89f87ce180bf96f0761616c4bf5200fb001b087</ID>
    </Owner>''' % f for f in self.__gcs_buckets[bucket])
    return '''\
<?xml version='1.0' encoding='UTF-8'?>
<ListBucketResult xmlns='http://doc.s3.amazonaws.com/2006-03-01'>
  <Name>io_infinit_test_backgrounds</Name>
  <Prefix>555dc10f4b82cf4fbd2159c1</Prefix>
  <Marker></Marker>
  <IsTruncated>false</IsTruncated>
  <Contents>
    %s
  </Contents>
</ListBucketResult>
''' % ''.join(content)

class Meta:

  def __init__(self,
               enable_emails = False,
               enable_invitations = False,
               force_admin = False,
               emailer = None,
               metrics = None,
               **kw):
    self.__mongo = mongobox.MongoBox()
    self.__server = bottle.TornadoServer(port = 0)
    self.__database = None
    self.__client = None
    self.__enable_emails = enable_emails
    self.__enable_invitations = enable_invitations
    self.__force_admin = force_admin
    self.__meta = None
    self.__meta_args = kw
    self.__metrics = metrics
    if 'shorten_ghost_profile_url' not in self.__meta_args:
      self.__meta_args['shorten_ghost_profile_url'] = False
    self.__emailer = emailer or TestEmailer()
    self.__version = infinit.oracles.meta.server.Meta.extract_version(Version.version, '')

  @property
  def emailer(self):
    return self.__emailer

  @property
  def version(self):
    return self.__version

  @property
  def domain(self):
    return "http://localhost:%s" % self.__server.port

  def __enter__(self):
    self.__mongo.__enter__()
    client = pymongo.MongoClient(port = self.__mongo.port)
    self.__database = client.meta
    client.meta.plans.insert({
      'name': 'basic',
      'quota': { 'total_link_size': 1e9},
      'features': {'nag': 'true'}
    })
    client.meta.plans.insert({
      'name': 'premium',
      'quota': { 'total_link_size': 5e10},
      'features': {'turbo': 'true'}
    })
    def run():
      try:
        self.__meta = InstrumentedMeta(
          mongo_port = self.__mongo.port,
          enable_emails = self.__enable_emails,
          enable_invitations = self.__enable_invitations,
          force_admin = self.__force_admin,
          emailer = self.__emailer,
          metrics = self.__metrics,
          **self.__meta_args)
        self.__meta.mailer = NoOpMailer()
        self.__meta.invitation = NoOpInvitation()
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
    while self.__server.port == 0 and self.__thread.is_alive():
      import time
      time.sleep(.1)
    return self

  @property
  def inner(self):
    return self.__meta

  @property
  def meta(self):
    return self.__meta

  @property
  def port(self):
    return self.__server.port

  @property
  def mailer(self):
    return self.__meta.mailer

  @mailer.setter
  def mailer(self, mailer):
    assert self.__meta is not None
    self.__meta.mailer = mailer

  @property
  def invitation(self):
    return self.__meta.invitation

  @invitation.setter
  def invitation(self, invitation):
    assert self.__meta is not None
    self.__meta.invitation = invitation

  @property
  def notifier(self):
    return self.__meta.notifier

  @notifier.setter
  def notifier(self, notifier):
    assert self.__meta is not None
    self.__meta.notifier = notifier

  def __exit__(self, *args, **kwargs):
    self.__mongo.__exit__(*args, **kwargs)

  def post(self, url, body = None, raw = False):
    return self.client.post(url, body, raw = raw)

  def get(self, url, body = None, raw = False):
    return self.client.get(url, body, raw = raw)

  def put(self, url, body = None, raw = False):
    return self.client.put(url, body, raw = raw)

  def delete(self, url, body = None, raw = False):
    return self.client.delete(url, body, raw = raw)

  def create_user(self,
                  email,
                  fullname = None,
                  password = '0' * 64,
                  source = None,
                  password_hash = None,
                  ):
    res = self.post('user/register',
                    {
                      'email': email,
                      'password': password,
                      'fullname': fullname or email.split('@')[0],
                      'source': source,
                      'password_hash': password_hash,
                    })
    assertEq(res['success'], True)
    return password

  @property
  def database(self):
    return self.__database

  @property
  def client(self):
    if self.__client is None:
      self.__client = Client(self)
    return self.__client

def throws(f, expected = None):
  guard = None
  if isinstance(expected, int):
    guard = lambda e: e.status == expected
    expected = HTTPException
  if expected is None:
    expected = BaseException
  try:
    f()
    assert False
  except expected as e:
    if isinstance(e, AssertionError):
      raise Exception('exception expected')
    if guard is not None:
      guard(e)
  else:
    raise Exception('exception expected')

def random_email(N = 10):
  import random
  from time import time
  random.seed(time())
  import string
  return ''.join(random.choice(string.ascii_lowercase + string.digits) for _ in range(N))

class User(Client):

  def __init__(self,
               meta,
               email = None,
               device_name = 'device',
               facebook = False,
               version = None,
               **kwargs):
    super().__init__(meta)

    if not facebook:
      self.email = email is not None and email or random_email() + '@infinit.io'
      self.password = meta.create_user(self.email, **kwargs)
      self.__id = meta.get('users/%s' % self.email)['id']
    else:
      self.__id = None
    self.device_id = uuid4()
    self.trophonius = None
    self.__version = version or \
                     infinit.oracles.meta.server.Meta.extract_version(
                       Version.version, '')

  def on_other_device(self):
    from copy import copy
    user_on_device = copy(self)
    user_on_device.device_id = uuid4()
    return user_on_device

  @property
  def version(self):
    return self.__version

  @version.setter
  def version(self, version):
    self.__version = version

  @property
  def id(self):
    if self.__id is None:
      self.__id = self.me['id']
    return self.__id

  @property
  def facebook_id(self):
    return self.me['facebook_id']

  @property
  def login_parameters(self):
    return {
      'email': self.email,
      'password': self.password,
      'device_id': str(self.device_id),
    }

  @property
  def data(self):
    res = self.get('user/self')
    return res

  def _login(self, res):
    assert 'self' in res
    assert 'device' in res
    self.compare_self_response(res['self'])
    assert res['device']['id'] == str(self.device_id)
    self.compare_device_response(res['device'])
    sync = self.get('user/synchronize?init=1')
    assert sync['success']
    res.update(sync)
    assert 'swaggers' in res
    assert res['swaggers'] == self.full_swaggers
    if self.trophonius is not None:
      self.trophonius.connect_user(self)
    return res

  def login(self, device_id = None, trophonius = None, **kw):
    if device_id is not None:
      self.device_id = device_id
    self.trophonius = trophonius
    params = self.login_parameters
    params.update(kw)
    params.update({'pick_trophonius': False})
    res = self.post('login', params)
    assertEq(res['success'], True)
    assertEq(res['account_registered'], False)
    self._login(res)
    return res

  def facebook_connect(self,
                       long_lived_access_token,
                       preferred_email = None,
                       no_device = False,
                       check_registration = None,
                       check_ghost_code = None):
    args = {
      'long_lived_access_token': long_lived_access_token
    }
    if preferred_email:
      args.update({
        'preferred_email': preferred_email})
    if self.device_id is not None and not no_device:
      args.update({
        'device_id': str(self.device_id)
      })
    if 'device_id' in args:
      res = self.post('login', args)
      if check_registration is not None:
        assertEq(res['account_registered'], check_registration)
      if check_ghost_code is not None:
        assert 'ghost_code' in res
      self._login(res)
    else:
      res = self.post('web-login', args)
    if 'success' in res:
      assert res['success']
    return res


  def __hash__(self):
    return str(self.id).__hash__()

  def logout(self):
    res = self.post('logout', {})
    assert res['success']
    if self.trophonius is not None:
      self.trophonius.disconnect_user(self)

  def synchronize(self, init = False):
    return self.get('user/synchronize?init=%s' % (init and '1' or '0'))

  @property
  def device(self):
    assert self.device_id is not None
    return self.get('device/%s/view' % str(self.device_id))

  @property
  def device_name(self):
    return self.device['name']

  @property
  def me(self):
    res = self.get('user/self')
    return res

  @property
  def accounts(self):
    return self.get('user/accounts')

  def compare_self_response(self, res):
    me = self.me
    assert res['public_key'] == me['public_key']
    assert res['identity'] == me['identity']
    assert res['handle'] == me['handle']
    assert res['email'] == me['email']
    assert res['id'] == me['id']
    assert res['fullname'] == me['fullname']

  def compare_device_response(self, res):
    device = self.device
    assert res['name'] == device['name']
    assert res['id'] == device['id']
    passport = res.get('passport', None)
    if passport is not None:
      assert passport == device['passport']

  @property
  def swaggers(self):
    return [s['id'] for s in self.full_swaggers]

  @property
  def full_swaggers(self):
    swaggers = self.get('user/swaggers')['swaggers']
    for swagger in swaggers:
      assertIn(swagger['register_status'], ['ok', 'deleted', 'ghost'])
    return swaggers

  @property
  def favorites(self):
    return self.data['favorites']

  @property
  def logged_in(self):
    try:
      res = self.data
      assert str(self.device_id) in res['devices']
      return True
    except HTTPException as e:
      assert e.status == 403
      return False

  @property
  def _id(self):
    return bson.ObjectId(self.data['id'])

  @property
  def devices(self):
    return self.get('user/devices')['devices']

  @property
  def avatar(self):
    return self.get('user/%s/avatar' % self.id)

  @property
  def connected(self):
    return self.get('user/%s/connected' % self.id)['connected']

  @property
  def identity(self):
    return self.data['identity']

  @property
  def fullname(self):
    return self.data['fullname']

  @property
  def transactions(self):
    res = self.get('transactions')
    assert res['success']
    return res['transactions']

  @property
  def connected_on_device(self, device_id = None):
    if device_id is None:
      device_id = self.device_id
    return self.get('device/%s/%s/connected' % (self.id, str(device_id)))['connected']

  def __eq__(self, other):
    if isinstance(other, User):
      return self.email == other.email
    return NotImplemented


  def sendfile(self,
               recipient,
               files = ['50% off books.pdf',
                        'a file with strange encoding: é.file',
                        'another file with no extension'],
               total_size = 2164062,
               message = 'no message',
               is_directory = False,
               device_id = None,
               initialize = False,
               use_identifier = False):
    if device_id is None:
      device_id = self.device_id

    transaction = {
      'files': files,
      'files_count': len(files),
      'total_size': total_size,
      'message': message,
      'is_directory': is_directory,
      'device_id': str(device_id),
      use_identifier and 'recipient_identifier' or 'id_or_email': recipient,
    }
    if not initialize:
      res = self.post('transaction/create', transaction)
      ghost = res['recipient_is_ghost']
    else:
      id = self.post('transaction/create_empty')['created_transaction_id']
      res = self.put('transaction/%s' % id, transaction)
      ghost = res['recipient_is_ghost']
    if ghost:
      self.get('transaction/%s/cloud_buffer' % res['created_transaction_id'])
    transaction.update({'_id': res['created_transaction_id']})
    return transaction, res

  def transaction_update(self, transaction, status):
    return self.post('transaction/update',
                     {
                       'transaction_id': str(transaction),
                       'status': status,
                       'device_id': str(self.device_id),
                       'device_name': self.device_name,
                     })

  # FIXME: remove when link & peer transactions are merged
  def getalink(self,
               files = [['file1', 42], ['file2', 43], ['file3', 44]],
               name = 'name',
               message = '',
               password = None,
               expiration_date = None):
    id = self.post('link_empty')['created_link_id']
    if expiration_date is not None:
      expiration_date = expiration_date.isoformat()
    return self.put(
      'link/%s' % id,
      {
        'expiration_date': expiration_date,
        'files': files,
        'name': name,
        'message': message,
        'password': password,
      })['transaction']

  # FIXME: remove when link & peer transactions are merged
  def link_update(self, link, status):
    self.post('link/%s' % link['id'],
              {
                'progress': 1,
                'status': status,
              })

# Fake facebook.
class Facebook:

  def __init__(self,
               broken_at = False):
    self.broken_at = broken_at
    self.__next_client_data = None

  @property
  def app_access_token(self):
    if self.broken_at == 'app_access_token':
      raise error.Error(error.EMAIL_PASSWORD_DONT_MATCH)
    return self.__app_access_token

  @property
  def next_client_data(self):
    if self.__next_client_data is None:
      self.__next_client_data = {
        'id': int(uniform(1000000000000000, 1500000000000000)),
        'email': None,
      }
    return self.__next_client_data

  def set_next_client_data(self, data):
    self.__next_client_data = data

  class Client:
    def __init__(self,
                 server,
                 long_lived_access_token,
                 id,
                 email):
      self.__server = server
      self.__long_lived_access_token = long_lived_access_token
      self.__data = None
      self.__data = {
        'name': 'Joseph Total',
        'first_name': 'Joseph',
        'last_name': 'Total',
        'id': str(id)
      }
      if email:
        self.__data.update({
          'email': email
        })

    @property
    def long_lived_access_token(self):
      if self.__server.broken_at == 'client_access_token':
        raise error.Error(error.EMAIL_PASSWORD_DONT_MATCH)
      if self.__access_token is None:
        self.__access_token = self.__server.client_access_token
      return self.__access_token

    @property
    def data(self):
      if self.__server.broken_at == 'client_data':
        raise error.Error(error.EMAIL_PASSWORD_DONT_MATCH)
      return self.__data

    @property
    def facebook_id(self):
      return str(self.data['id'])

    @property
    def permissions(self):
      pass

    @property
    def friends(self):
      return {'data': []}

  def user(self,
           code = None,
           short_lived_access_token = None,
           long_lived_access_token = None):
    data = self.next_client_data
    return Facebook.Client(
      server = self,
      long_lived_access_token = long_lived_access_token,
      id = data['id'],
      email = data['email'],
    )

def assertEq(a, b):
  if a != b:
    raise Exception('%r != %r' % (a, b))

def assertNeq(a, b):
  if a == b:
    raise Exception('%r == %r' % (a, b))

def assertIn(e, c):
  if e not in c:
    raise Exception('%r not in %r' % (e, c))

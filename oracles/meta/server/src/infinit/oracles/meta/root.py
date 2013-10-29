# -*- encoding: utf-8 -*-

import time

from . import conf, mail, error
from .utils import api, require_admin
import infinit.oracles.meta.version

LOST_PASSWORD_TEMPLATE_ID = 'lost-password'
RESET_PASSWORD_VALIDITY = 2 * 3600 # 2 hours

class Mixin:

  @api('/')
  def root(self):
    return self.success({
        'server': 'Meta %s' % infinit.oracles.meta.version.version,
        'logged_in': self.user is not None,
        # 'fallbacks': str(self.__application__.fallback),
    })

  @api('/status')
  def status(self):
    return self.success({"status" : "ok"})

  @api('/ghostify', method = 'POST')
  @require_admin
  def ghostify(self, email, admin_gtoken):
    email.strip()

    user = self.database.users.find_one({"email": email})

    if user is None:
      return self.error(error.UNKNOWN_USER)

    # Invalidate all transactions.
    # XXX: Peers should be notified.
    from meta.resources import transaction
    self.database.transactions.update(
      {"$or": [{"sender_id": user['_id']}, {"recipient_id": user['_id']}]},
      {"$set": {"status": transaction.CANCELED}}, multi=True)

    keys = ['_id', 'email', 'fullname', 'ghost', 'swaggers', 'accounts',
            'remaining_invitations', 'handle', 'lw_handle']

    ghost = dict()
    for key in keys:
      value = user.get(key)
      if value is not None:
        ghost[key] = value

    # Ghostify user.
    ghost = self.registerUser(**ghost)

    from meta.invitation import invite_user
    invitation.invite_user(user['email'], database = self.database)

    return self.success({'ghost': str(user['_id'])})

  def __user_from_hash(self, hash):
    user = self.database.users.find_one({"reset_password_hash": hash})
    if user is None:
      self.raise_error(
        error.OPERATION_NOT_PERMITTED,
        msg = "Your password has already been reset",
        )
    if user['reset_password_hash_validity'] < time.time():
      self.raise_error(
        error.OPERATION_NOT_PERMITTED,
        msg = "The reset url is not valid anymore",
      )
    return user

  @api('reset-accounts/<hash>')
  def reseted_account(self, hash):
    """Reset account using the hash generated from the /lost-password page.

    hash -- the reset password token.
    """

    usr = self.__user_from_hash(hash)
    return self.success(
      {
        'email': usr['email'],
      }
    )

  @api('reset-accounts/<hash>')
  def reset_account(self, hash):
    user = self.__user_from_hash(hash)
    from meta.resources import transaction

    for transaction_id in self.database.transactions.find(
        {
          "$or": [
            {"sender_id": user['_id']},
            {"recipient_id": user['_id']}
          ]
        },
        fields = ['_id']):
      try:
        self._transaction_update(transaction_id, transaction.CANCELED, user)
      except error.Error as e:
        self.fail(error.UNKNOWN)

    import metalib
    from meta import conf
    identity, public_key = metalib.generate_identity(
      str(user["_id"]),
      user['email'], self.data['password'],
      conf.INFINIT_AUTHORITY_PATH,
      conf.INFINIT_AUTHORITY_PASSWORD
    )

    user_id = self.registerUser(
      _id = user["_id"],
      register_status = 'ok',
      email = user['email'],
      fullname = user['fullname'],
      password = self.hashPassword(self.data['password']),
      identity = identity,
      public_key = public_key,
      handle = user['handle'],
      lw_handle = user['lw_handle'],
      swaggers = user['swaggers'],
      networks = [],
      devices = [],
      connected_devices = [],
      connected = False,
      notifications = [],
      old_notifications = [],
      accounts = [
        {'type': 'email', 'id': user['email']}
        ],
      remaining_invitations = user['remaining_invitations'],
      )
    return self.success({'user_id': str(user_id)})

  @api('/lost-password')
  def declare_lost_password(self, email):
    """Generate a reset password url.

    email -- The mail of the infortunate user
    """

    email = email.lower()
    user = self.database.users.find_one({"email": email})
    if not user:
      return self.error(error_code = error.UNKNOWN_USER)
    import time, hashlib
    self.database.users.update(
      {'email': email},
      {'$set':
        {
          'reset_password_hash': hashlib.md5(str(time.time()) + email).hexdigest(),
          'reset_password_hash_validity': time.time() + RESET_PASSWORD_VALIDITY,
        }
      })

    user = self.database.users.find_one({'email': email}, fields = ['reset_password_hash'])
    from meta.mail import send_via_mailchimp
    self.mailer.send_via_mailchimp(
      email,
      LOST_PASSWORD_TEMPLATE_ID,
      '[Infinit] Reset your password',
      reply_to = 'support@infinit.io',
      reset_password_hash = user['reset_password_hash'],
    )

    return self.success()

  @api('/debug/user-report', method = 'POST')
  def user_report(self,
                  user_name = 'Unknown',
                  client_os = 'Unknown',
                  message = [],
                  env = [],
                  version = 'Unknown version',
                  email = 'crash@infinit.io',
                  send = False,
                  file = ''):
    """
    Store the existing crash into database and send a mail if set.
    """
    if send:
      self.mailer.send(
        email,
        subject = mail.USER_REPORT_SUBJECT % {"client_os": client_os,},
        content = mail.USER_REPORT_CONTENT % {
          "client_os": client_os,
          "version": version,
          "user_name": user_name,
          "env":  '\n'.join(env),
          "message": message,
        },
        attached = file
      )

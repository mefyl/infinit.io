# -*- encoding: utf-8 -*-

import elle.log
from . import conf, mail
import decorator

def _generate_code(email):
  import hashlib
  import time
  hash_ = hashlib.md5()
  hash_.update(email.encode('utf8') + str(time.time()).encode('utf8'))
  return hash_.hexdigest()

ELLE_LOG_COMPONENT = 'infinit.oracles.meta.server.Invitation'

XXX_MAILCHIMP_SUCKS_TEMPLATE_SUBJECTS = {
  'invitation-beta': '%(sendername)s would like to invite you to Infinit',
  'send-file': '%(sendername)s wants to share %(filename)s with you',
  'send-invitation-no-file': '%(sendername)s wants to use Infinit with you',
}

ALPHA_LIST = 'd8d5225ac7'
INVITED_LIST = '385e50ea2c'
USERBASE_LIST = 'cf5bcab5b1'

class Invitation:

  def __init__(self,
               active = True):
    self.__active = active
    from mailsnake import MailSnake
    self.ms = MailSnake(conf.MAILCHIMP_APIKEY)

  def is_active(method):
    def wrapper(wrapped, self, *a, **ka):
      if not self.__active:
        print("invitation was ignored because inviter is inactive")
        return # Return an empty func.
      return wrapped(self, *a, **ka)
    return decorator.decorator(wrapper, method)

  @is_active
  def move_from_invited_to_userbase(self, ghost_mail, new_mail):
    try:
      self.ms.listUnsubscribe(id = INVITED_LIST, email_address = ghost_mail)
    except:
      print("Couldn't unsubscribe", ghost_mail, "from INVITED")
    self.subscribe(new_mail)

  @is_active
  def subscribe(self, email):
    try:
      ms.listSubscribe(id = USERBASE_LIST,
                       email_address = mail,
                       double_optin = False)
    except:
      print("Couldn't subscribe", mail, "to USERBASE")

def invite_user(email,
                mailer,
                send_email = True,
                source = 'Infinit <no-reply@infinit.io>',
                mail_template = 'send-invitation-no-file',
                database = None,
                **kw):
  with elle.log.trace('invite user %s' % email):
    assert database is not None
    code = _generate_code(email)
    database.invitations.insert({
      'email': email,
      'status': 'pending',
      'code': code,
      'source': source[1],
    })
    subject = XXX_MAILCHIMP_SUCKS_TEMPLATE_SUBJECTS[mail_template] % kw
    elle.log.debug('subject: %s' % subject)
    if send_email:
      mailer.templated_send(
        to = email,
        template_id = mail_template,
        subject = subject,
        fr = "%s <%s>" % source,
        reply_to = source,
        #  accesscode=code,
        **kw
      )
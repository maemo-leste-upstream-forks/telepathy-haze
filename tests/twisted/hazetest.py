
"""
Infrastructure code for testing Haze by pretending to be a Jabber server.
"""

import base64
import os
import sha
import sys
import time
import random

import ns
import servicetest
import twisted
from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ
from twisted.words.protocols.jabber import xmlstream
from twisted.internet import reactor

import dbus

NS_XMPP_SASL = 'urn:ietf:params:xml:ns:xmpp-sasl'
NS_XMPP_BIND = 'urn:ietf:params:xml:ns:xmpp-bind'

def make_result_iq(stream, iq):
    result = IQ(stream, "result")
    result["id"] = iq["id"]
    query = iq.firstChildElement()

    if query:
        result.addElement((query.uri, query.name))

    return result

def acknowledge_iq(stream, iq):
    stream.send(make_result_iq(stream, iq))

def send_error_reply(stream, iq):
    result = IQ(stream, "error")
    result["id"] = iq["id"]
    query = iq.firstChildElement()

    if query:
        result.addElement((query.uri, query.name))

    stream.send(result)

def request_muc_handle(q, conn, stream, muc_jid):
    servicetest.call_async(q, conn, 'RequestHandles', 2, [muc_jid])
    host = muc_jid.split('@')[1]
    event = q.expect('stream-iq', to=host, query_ns=ns.DISCO_INFO)
    result = make_result_iq(stream, event.stanza)
    feature = result.firstChildElement().addElement('feature')
    feature['var'] = ns.MUC
    stream.send(result)
    event = q.expect('dbus-return', method='RequestHandles')
    return event.value[0][0]

def make_muc_presence(affiliation, role, muc_jid, alias, jid=None):
    presence = domish.Element((None, 'presence'))
    presence['from'] = '%s/%s' % (muc_jid, alias)
    x = presence.addElement((ns.MUC_USER, 'x'))
    item = x.addElement('item')
    item['affiliation'] = affiliation
    item['role'] = role
    if jid is not None:
        item['jid'] = jid
    return presence

def sync_stream(q, stream):
    """Used to ensure that the CM has processed all stanzas sent to it."""

    iq = IQ(stream, "get")
    id = iq['id']
    iq.addElement(('http://jabber.org/protocol/disco#info', 'query'))
    stream.send(iq)
    q.expect('stream-iq', query_ns='http://jabber.org/protocol/disco#info',
        predicate=(lambda event:
            event.stanza['id'] == id and event.iq_type == 'result'))

class JabberAuthenticator(xmlstream.Authenticator):
    "Trivial XML stream authenticator that accepts one username/digest pair."

    def __init__(self, username, password):
        self.username = username
        self.password = password
        xmlstream.Authenticator.__init__(self)

    # Patch in fix from http://twistedmatrix.com/trac/changeset/23418.
    # This monkeypatch taken from Gadget source code
    from twisted.words.xish.utility import EventDispatcher

    def _addObserver(self, onetime, event, observerfn, priority, *args,
            **kwargs):
        if self._dispatchDepth > 0:
            self._updateQueue.append(lambda: self._addObserver(onetime, event,
                observerfn, priority, *args, **kwargs))

        return self._oldAddObserver(onetime, event, observerfn, priority,
            *args, **kwargs)

    EventDispatcher._oldAddObserver = EventDispatcher._addObserver
    EventDispatcher._addObserver = _addObserver

    def streamStarted(self, root=None):
        if root:
            self.xmlstream.sid = '%x' % random.randint(1, sys.maxint)

        self.xmlstream.sendHeader()
        self.xmlstream.addOnetimeObserver(
            "/iq/query[@xmlns='jabber:iq:auth']", self.initialIq)

    def initialIq(self, iq):
        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        query = result.addElement('query')
        query["xmlns"] = "jabber:iq:auth"
        query.addElement('username', content='test')
        query.addElement('password')
        query.addElement('digest')
        query.addElement('resource')
        self.xmlstream.addOnetimeObserver('/iq/query/username', self.secondIq)
        self.xmlstream.send(result)

    def secondIq(self, iq):
        username = xpath.queryForNodes('/iq/query/username', iq)
        assert map(str, username) == [self.username]

        digest = xpath.queryForNodes('/iq/query/digest', iq)
        expect = sha.sha(self.xmlstream.sid + self.password).hexdigest()
        assert map(str, digest) == [expect]

        resource = xpath.queryForNodes('/iq/query/resource', iq)
        assert map(str, resource) == ['Resource']

        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        self.xmlstream.send(result)
        self.xmlstream.dispatch(self.xmlstream, xmlstream.STREAM_AUTHD_EVENT)


class XmppAuthenticator(xmlstream.Authenticator):
    def __init__(self, username, password):
        xmlstream.Authenticator.__init__(self)
        self.username = username
        self.password = password
        self.authenticated = False

    def streamStarted(self, root=None):
        if root:
            self.xmlstream.sid = root.getAttribute('id')

        self.xmlstream.sendHeader()

        if self.authenticated:
            # Initiator authenticated itself, and has started a new stream.

            features = domish.Element((xmlstream.NS_STREAMS, 'features'))
            bind = features.addElement((NS_XMPP_BIND, 'bind'))
            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver(
                "/iq/bind[@xmlns='%s']" % NS_XMPP_BIND, self.bindIq)
        else:
            features = domish.Element((xmlstream.NS_STREAMS, 'features'))
            mechanisms = features.addElement((NS_XMPP_SASL, 'mechanisms'))
            mechanism = mechanisms.addElement('mechanism', content='PLAIN')
            self.xmlstream.send(features)

            self.xmlstream.addOnetimeObserver("/auth", self.auth)

    def auth(self, auth):
        assert (base64.b64decode(str(auth)) ==
            '\x00%s\x00%s' % (self.username, self.password))

        success = domish.Element((NS_XMPP_SASL, 'success'))
        self.xmlstream.send(success)
        self.xmlstream.reset()
        self.authenticated = True

    def bindIq(self, iq):
        assert xpath.queryForString('/iq/bind/resource', iq) == 'Resource'

        result = IQ(self.xmlstream, "result")
        result["id"] = iq["id"]
        bind = result.addElement((NS_XMPP_BIND, 'bind'))
        jid = bind.addElement('jid', content='test@localhost/Resource')
        self.xmlstream.send(result)

        self.xmlstream.dispatch(self.xmlstream, xmlstream.STREAM_AUTHD_EVENT)

def make_stream_event(type, stanza):
    event = servicetest.Event(type, stanza=stanza)
    event.to = stanza.getAttribute("to")
    return event

def make_iq_event(iq):
    event = make_stream_event('stream-iq', iq)
    event.iq_type = iq.getAttribute("type")
    query = iq.firstChildElement()

    if query:
        event.query = query
        event.query_ns = query.uri
        event.query_name = query.name

        if query.getAttribute("node"):
            event.query_node = query.getAttribute("node")

    return event

def make_presence_event(stanza):
    event = make_stream_event('stream-presence', stanza)
    event.presence_type = stanza.getAttribute('type')
    return event

def make_message_event(stanza):
    event = make_stream_event('stream-message', stanza)
    event.message_type = stanza.getAttribute('type')
    return event

class BaseXmlStream(xmlstream.XmlStream):
    initiating = False
    namespace = 'jabber:client'

    def __init__(self, event_func, authenticator):
        xmlstream.XmlStream.__init__(self, authenticator)
        self.event_func = event_func
        self.addObserver('//iq', lambda x: event_func(
            make_iq_event(x)))
        self.addObserver('//message', lambda x: event_func(
            make_message_event(x)))
        self.addObserver('//presence', lambda x: event_func(
            make_presence_event(x)))
        self.addObserver('//event/stream/authd', self._cb_authd)

    def _cb_authd(self, _):
        # called when stream is authenticated
        self.addObserver(
            "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
            self._cb_disco_iq)
        self.event_func(servicetest.Event('stream-authenticated'))

    def _cb_disco_iq(self, iq):
        if iq.getAttribute('to') == 'localhost':
            # add PEP support
            nodes = xpath.queryForNodes(
                "/iq/query[@xmlns='http://jabber.org/protocol/disco#info']",
                iq)
            query = nodes[0]
            identity = query.addElement('identity')
            identity['category'] = 'pubsub'
            identity['type'] = 'pep'

            iq['type'] = 'result'
            self.send(iq)

class JabberXmlStream(BaseXmlStream):
    version = (0, 9)

class XmppXmlStream(BaseXmlStream):
    version = (1, 0)

def make_connection(bus, event_func, params=None):
    default_params = {
        'account': 'test@localhost/Resource',
        'password': 'pass',
        #'resource': 'Resource',
        'server': 'localhost',
        # FIXME: the spec says this is a UInt32 and Gabble agrees
        'port': dbus.Int32(4242),
        }

    if params:
        default_params.update(params)

    return servicetest.make_connection(bus, event_func, 'haze', 'jabber',
        default_params)

def make_stream(event_func, authenticator=None, protocol=None, port=4242):
    # set up Jabber server

    if authenticator is None:
        authenticator = JabberAuthenticator('test', 'pass')

    if protocol is None:
        protocol = JabberXmlStream

    stream = protocol(event_func, authenticator)
    factory = twisted.internet.protocol.Factory()
    factory.protocol = lambda *args: stream
    port = reactor.listenTCP(port, factory)
    return (stream, port)

def go(params=None, authenticator=None, protocol=None, start=None):
    # hack to ease debugging
    domish.Element.__repr__ = domish.Element.toXml

    bus = dbus.SessionBus()
    handler = servicetest.EventTest()
    conn = make_connection(bus, handler.handle_event, params)
    (stream, _) = make_stream(handler.handle_event, authenticator, protocol)
    handler.data = {
        'bus': bus,
        'conn': conn,
        'conn_iface': dbus.Interface(conn,
            'org.freedesktop.Telepathy.Connection'),
        'stream': stream}
    handler.data['test'] = handler
    handler.verbose = (os.environ.get('CHECK_TWISTED_VERBOSE', '') != '')
    map(handler.expect, servicetest.load_event_handlers())

    if '-v' in sys.argv:
        handler.verbose = True

    if start is None:
        handler.data['conn'].Connect()
    else:
        start(handler.data)

    reactor.run()

def install_colourer():
    def red(s):
        return '\x1b[31m%s\x1b[0m' % s

    def green(s):
        return '\x1b[32m%s\x1b[0m' % s

    patterns = {
        'handled': green,
        'not handled': red,
        }

    class Colourer:
        def __init__(self, fh, patterns):
            self.fh = fh
            self.patterns = patterns

        def write(self, s):
            f = self.patterns.get(s, lambda x: x)
            self.fh.write(f(s))

    sys.stdout = Colourer(sys.stdout, patterns)
    return sys.stdout


def exec_test_deferred (funs, params, protocol=None, timeout=None):
    # hack to ease debugging
    domish.Element.__repr__ = domish.Element.toXml
    colourer = None

    if sys.stdout.isatty():
        colourer = install_colourer()

    queue = servicetest.IteratingEventQueue(timeout)
    queue.verbose = (
        os.environ.get('CHECK_TWISTED_VERBOSE', '') != ''
        or '-v' in sys.argv)

    bus = dbus.SessionBus()
    # conn = make_connection(bus, queue.append, params)
    (stream, port) = make_stream(queue.append, protocol=protocol)

    error = None

    try:
        for f in funs:
            conn = make_connection(bus, queue.append, params)
            f(queue, bus, conn, stream)
    except Exception, e:
        import traceback
        traceback.print_exc()
        error = e

    try:
        if colourer:
          sys.stdout = colourer.fh
        d = port.stopListening()
        if error is None:
            d.addBoth((lambda *args: reactor.crash()))
        else:
            # please ignore the POSIX behind the curtain
            d.addBoth((lambda *args: os._exit(1)))

        conn.Disconnect()

    except dbus.DBusException, e:
        pass

def exec_tests(funs, params=None, protocol=None, timeout=None):
  reactor.callWhenRunning (exec_test_deferred, funs, params, protocol, timeout)
  reactor.run()

def exec_test(fun, params=None, protocol=None, timeout=None):
  exec_tests([fun], params, protocol, timeout)

# Useful routines for server-side vCard handling
current_vcard = domish.Element(('vcard-temp', 'vCard'))

def handle_get_vcard(event, data):
    iq = event.stanza

    if iq['type'] != 'get':
        return False

    if iq.uri != 'jabber:client':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    # Send back current vCard
    new_iq = IQ(data['stream'], 'result')
    new_iq['id'] = iq['id']
    new_iq.addChild(current_vcard)
    data['stream'].send(new_iq)
    return True

def handle_set_vcard(event, data):
    global current_vcard
    iq = event.stanza

    if iq['type'] != 'set':
        return False

    if iq.uri != 'jabber:client':
        return False

    vcard = list(iq.elements())[0]

    if vcard.name != 'vCard':
        return False

    current_vcard = iq.firstChildElement()

    new_iq = IQ(data['stream'], 'result')
    new_iq['id'] = iq['id']
    data['stream'].send(new_iq)
    return True


def _elem_add(elem, *children):
    for child in children:
        if isinstance(child, domish.Element):
            elem.addChild(child)
        elif isinstance(child, unicode):
            elem.addContent(child)
        else:
            raise ValueError('invalid child object %r', child)

def elem(a, b=None, **kw):
    r"""
    >>> elem('foo')().toXml()
    u'<foo/>'
    >>> elem('foo', x='1')().toXml()
    u"<foo x='1'/>"
    >>> elem('foo', x='1')(u'hello').toXml()
    u"<foo x='1'>hello</foo>"
    >>> elem('foo', x='1')(u'hello',
    ...         elem('http://foo.org', 'bar', y='2')(u'bye')).toXml()
    u"<foo x='1'>hello<bar xmlns='http://foo.org' y='2'>bye</bar></foo>"
    """

    class _elem(domish.Element):
        def __call__(self, *children):
            _elem_add(self, *children)
            return self

    if b is not None:
        elem = _elem((a, b))
    else:
        elem = _elem((None, a))

    for k, v in kw.iteritems():
        if k == 'from_':
            elem['from'] = v
        else:
            elem[k] = v

    return elem

def elem_iq(server, type, **kw):
    class _iq(IQ):
        def __call__(self, *children):
            _elem_add(self, *children)
            return self

    iq = _iq(server, type)

    for k, v in kw.iteritems():
        if k == 'from_':
            iq['from'] = v
        else:
            iq[k] = v

    return iq

def make_presence(_from, to='test@localhost', type=None, status=None, caps=None):
    presence = domish.Element((None, 'presence'))
    presence['from'] = _from
    presence['to'] = to

    if type is not None:
        presence['type'] = type

    if status is not None:
        presence.addElement('status', content=status)

    if caps is not None:
        cel = presence.addElement(('http://jabber.org/protocol/caps', 'c'))
        for key,value in caps.items():
            cel[key] = value

    return presence

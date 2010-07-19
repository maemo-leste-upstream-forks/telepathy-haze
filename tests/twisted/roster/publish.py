"""
Test requests to see our presence.
"""

import dbus

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish

from servicetest import (EventPattern, wrap_channel, assertLength,
        assertEquals, call_async, sync_dbus)
from hazetest import acknowledge_iq, exec_test, sync_stream
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    call_async(q, conn.Requests, 'EnsureChannel',{
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_LIST,
        cs.TARGET_ID: 'publish',
        })
    e = q.expect('dbus-return', method='EnsureChannel')
    publish = wrap_channel(bus.get_object(conn.bus_name, e.value[1]),
            cs.CHANNEL_TYPE_CONTACT_LIST)
    jids = set(conn.InspectHandles(cs.HT_CONTACT, publish.Group.GetMembers()))
    assertEquals(set(), jids)

    call_async(q, conn.Requests, 'EnsureChannel',{
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_LIST,
        cs.TARGET_ID: 'subscribe',
        })
    e = q.expect('dbus-return', method='EnsureChannel')
    subscribe = wrap_channel(bus.get_object(conn.bus_name, e.value[1]),
            cs.CHANNEL_TYPE_CONTACT_LIST)
    jids = set(conn.InspectHandles(cs.HT_CONTACT, subscribe.Group.GetMembers()))
    assertEquals(set(), jids)

    # receive a subscription request
    alice = conn.RequestHandles(cs.HT_CONTACT, ['alice@wonderland.lit'])[0]

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'alice@wonderland.lit'
    presence['type'] = 'subscribe'
    presence.addElement('status', content='friend me')
    stream.send(presence)

    # it seems either libpurple or haze doesn't pass the message through
    q.expect('dbus-signal', path=publish.object_path,
            args=['', [], [], [alice], [], alice,
                cs.GC_REASON_NONE])

    # accept
    call_async(q, publish.Group, 'AddMembers', [alice], '')

    q.expect_many(
            EventPattern('stream-presence', presence_type='subscribed',
                to='alice@wonderland.lit'),
            EventPattern('dbus-signal', signal='MembersChanged',
                path=publish.object_path,
                args=['', [alice], [], [], [], conn.GetSelfHandle(),
                    cs.GC_REASON_NONE]),
            EventPattern('dbus-return', method='AddMembers'),
            )

    # the server sends us a roster push
    iq = IQ(stream, 'set')
    iq['id'] = 'roster-push'
    query = iq.addElement(('jabber:iq:roster', 'query'))
    item = query.addElement('item')
    item['jid'] = 'alice@wonderland.lit'
    item['subscription'] = 'from'

    stream.send(iq)

    _, _, new_group = q.expect_many(
            EventPattern('stream-iq', iq_type='result',
                predicate=lambda e: e.stanza['id'] == 'roster-push'),
            # this isn't really true, but it's the closest we can guess from
            # libpurple
            EventPattern('dbus-signal', signal='MembersChanged',
                path=subscribe.object_path,
                args=['', [alice], [], [], [], 0, cs.GC_REASON_NONE]),
            # the buddy needs a group, because libpurple
            EventPattern('dbus-signal', signal='NewChannels',
                predicate=lambda e:
                    e.args[0][0][1].get(cs.CHANNEL_TYPE) ==
                        cs.CHANNEL_TYPE_CONTACT_LIST and
                    e.args[0][0][1].get(cs.TARGET_HANDLE_TYPE) ==
                        cs.HT_GROUP),
            )

    def_group = wrap_channel(bus.get_object(conn.bus_name,
        new_group.args[0][0][0]), cs.CHANNEL_TYPE_CONTACT_LIST)

    assertEquals(set([alice]), set(def_group.Group.GetMembers()))

    # receive another subscription request
    queen = conn.RequestHandles(cs.HT_CONTACT,
            ['queen.of.hearts@wonderland.lit'])[0]

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'queen.of.hearts@wonderland.lit'
    presence['type'] = 'subscribe'
    presence.addElement('status', content='Off with her head!')
    stream.send(presence)

    # it seems either libpurple or haze doesn't pass the message through
    q.expect('dbus-signal', path=publish.object_path,
            args=['', [], [], [queen], [], queen,
                cs.GC_REASON_NONE])

    # decline
    call_async(q, publish.Group, 'RemoveMembers', [queen], '')

    q.expect_many(
            EventPattern('stream-presence', presence_type='unsubscribed',
                to='queen.of.hearts@wonderland.lit'),
            EventPattern('dbus-signal', signal='MembersChanged',
                path=publish.object_path,
                args=['', [], [queen], [], [], conn.GetSelfHandle(),
                    cs.GC_REASON_NONE]),
            EventPattern('dbus-return', method='RemoveMembers'),
            )

    sync_dbus(bus, q, conn)
    sync_stream(q, stream)

    # the declined contact isn't on our roster
    assertEquals(set([alice]), set(def_group.Group.GetMembers()))

if __name__ == '__main__':
    exec_test(test)

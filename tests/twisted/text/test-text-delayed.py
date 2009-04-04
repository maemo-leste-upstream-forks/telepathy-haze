
"""
Test receiving delayed (offline) messages on a text channel.
"""

import datetime

from twisted.words.xish import domish

from hazetest import exec_test
from servicetest import EventPattern

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com'
    m['type'] = 'chat'
    m.addElement('body', content='hello')

    # add timestamp information
    x = m.addElement(('jabber:x:delay', 'x'))
    x['stamp'] = '20070517T16:15:01'

    stream.send(m)

    event = q.expect('dbus-signal', signal='NewChannel')
    assert event.args[1] == u'org.freedesktop.Telepathy.Channel.Type.Text'
    # check that handle type == contact handle
    assert event.args[2] == 1
    jid = conn.InspectHandles(1, [event.args[3]])[0]
    assert jid == 'foo@bar.com'

    received, message_received = q.expect_many(
        EventPattern('dbus-signal', signal='Received'),
        EventPattern('dbus-signal', signal='MessageReceived'),
        )

    old_signal_time = str(datetime.datetime.utcfromtimestamp(received.args[1]))
    assert old_signal_time == '2007-05-17 16:15:01', old_signal_time
    assert received.args[5] == 'hello'

    message = message_received.args[0]
    header = message[0]
    message_sent_timestamp = header['message-sent']
    new_signal_time = str(datetime.datetime.utcfromtimestamp(message_sent_timestamp))
    assert new_signal_time == '2007-05-17 16:15:01', (headers, new_signal_time)
    message_received_timestamp = header['message-received']
    assert message_received_timestamp > message_sent_timestamp, headers

    assert message[1]['content'] == 'hello', message


    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)


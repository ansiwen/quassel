/***************************************************************************
 *   Copyright (C) 2005-08 by the Quassel Project                          *
 *   devel@quassel-irc.org                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3.                                           *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/
#include "userinputhandler.h"

#include "util.h"

#include "networkconnection.h"
#include "network.h"
#include "ctcphandler.h"

#include <QDebug>

UserInputHandler::UserInputHandler(NetworkConnection *parent) : BasicHandler(parent) {
}

void UserInputHandler::handleUserInput(const BufferInfo &bufferInfo, const QString &msg_) {
  try {
    if(msg_.isEmpty())
      return;
    QString cmd;
    QString msg = msg_;
    if(!msg.startsWith('/')) {
      cmd = QString("SAY");
    } else {
      cmd = msg.section(' ', 0, 0).remove(0, 1).toUpper();
      msg = msg.section(' ', 1);
    }
    handle(cmd, Q_ARG(BufferInfo, bufferInfo), Q_ARG(QString, msg));
  } catch(Exception e) {
    emit displayMsg(Message::Error, bufferInfo.type(), "", e.msg());
  }
}

// ====================
//  Public Slots
// ====================

void UserInputHandler::handleAway(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  putCmd("AWAY", serverEncode(msg));
}

void UserInputHandler::handleBan(const BufferInfo &bufferInfo, const QString &msg) {
  if(bufferInfo.type() != BufferInfo::ChannelBuffer)
    return;
  
  //TODO: find suitable default hostmask if msg gives only nickname 
  // Example: MODE &oulu +b *!*@*
  QByteArray banMsg = serverEncode(bufferInfo.bufferName()) + " +b " + channelEncode(bufferInfo.bufferName(), msg);
  emit putCmd("MODE", banMsg);
}

void UserInputHandler::handleCtcp(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  QString nick = msg.section(' ', 0, 0);
  QString ctcpTag = msg.section(' ', 1, 1).toUpper();
  if (ctcpTag.isEmpty()) return;
  QString message = "";
  QString verboseMessage = tr("sending CTCP-%1 request").arg(ctcpTag);

  if(ctcpTag == "PING") {
    uint now = QDateTime::currentDateTime().toTime_t();
    message = QString::number(now);
  }

  networkConnection()->ctcpHandler()->query(nick, ctcpTag, message);
  emit displayMsg(Message::Action, BufferInfo::StatusBuffer, "", verboseMessage, network()->myNick());
}

void UserInputHandler::handleDeop(const BufferInfo &bufferInfo, const QString &msg) {
  QStringList nicks = msg.split(' ', QString::SkipEmptyParts);
  QString m = "-"; for(int i = 0; i < nicks.count(); i++) m += 'o';
  QStringList params;
  params << bufferInfo.bufferName() << m << nicks;
  emit putCmd("MODE", serverEncode(params));
}

void UserInputHandler::handleDevoice(const BufferInfo &bufferInfo, const QString &msg) {
  QStringList nicks = msg.split(' ', QString::SkipEmptyParts);
  QString m = "-"; for(int i = 0; i < nicks.count(); i++) m += 'v';
  QStringList params;
  params << bufferInfo.bufferName() << m << nicks;
  emit putCmd("MODE", serverEncode(params));
}

void UserInputHandler::handleInvite(const BufferInfo &bufferInfo, const QString &msg) {
  QStringList params;
  params << msg << bufferInfo.bufferName();
  emit putCmd("INVITE", serverEncode(params));
}

void UserInputHandler::handleJ(const BufferInfo &bufferInfo, const QString &msg) {
  QString trimmed = msg.trimmed();
  if(trimmed.length() == 0) return;
  if(trimmed[0].isLetter()) trimmed.prepend("#");
  handleJoin(bufferInfo, trimmed);
}

void UserInputHandler::handleJoin(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  QStringList params = msg.trimmed().split(" ");
  QStringList chans = params[0].split(",");
  QStringList keys;
  if(params.count() > 1) keys = params[1].split(",");
  emit putCmd("JOIN", serverEncode(params)); // FIXME handle messages longer than 512 bytes!
  int i = 0;
  for(; i < keys.count(); i++) {
    if(i >= chans.count()) break;
    networkConnection()->addChannelKey(chans[i], keys[i]);
  }
  for(; i < chans.count(); i++) {
    networkConnection()->removeChannelKey(chans[i]);
  }
}

void UserInputHandler::handleKick(const BufferInfo &bufferInfo, const QString &msg) {
  QString nick = msg.section(' ', 0, 0, QString::SectionSkipEmpty);
  QString reason = msg.section(' ', 1, -1, QString::SectionSkipEmpty).trimmed();
  if(reason.isEmpty()) reason = networkConnection()->identity()->kickReason();
  QList<QByteArray> params;
  params << serverEncode(bufferInfo.bufferName()) << serverEncode(nick) << channelEncode(bufferInfo.bufferName(), reason);
  emit putCmd("KICK", params);
}

void UserInputHandler::handleList(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  emit putCmd("LIST", serverEncode(msg.split(' ', QString::SkipEmptyParts)));
}


void UserInputHandler::handleMe(const BufferInfo &bufferInfo, const QString &msg) {
  if(bufferInfo.bufferName().isEmpty()) return; // server buffer
  networkConnection()->ctcpHandler()->query(bufferInfo.bufferName(), "ACTION", msg);
  emit displayMsg(Message::Action, bufferInfo.type(), bufferInfo.bufferName(), msg, network()->myNick());
}

void UserInputHandler::handleMode(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  // TODO handle correct encoding for buffer modes (channelEncode())
  emit putCmd("MODE", serverEncode(msg.split(' ', QString::SkipEmptyParts)));
}

// TODO: show privmsgs
void UserInputHandler::handleMsg(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo);
  if(!msg.contains(' '))
    return;

  QList<QByteArray> params;
  params << serverEncode(msg.section(' ', 0, 0));
  params << userEncode(params[0], msg.section(' ', 1));

  emit putCmd("PRIVMSG", params);
}

void UserInputHandler::handleNick(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  QString nick = msg.section(' ', 0, 0);
  emit putCmd("NICK", serverEncode(nick));
}

void UserInputHandler::handleOp(const BufferInfo &bufferInfo, const QString &msg) {
  QStringList nicks = msg.split(' ', QString::SkipEmptyParts);
  QString m = "+"; for(int i = 0; i < nicks.count(); i++) m += 'o';
  QStringList params;
  params << bufferInfo.bufferName() << m << nicks;
  emit putCmd("MODE", serverEncode(params));
}

void UserInputHandler::handlePart(const BufferInfo &bufferInfo, const QString &msg) {
  QList<QByteArray> params;
  params << serverEncode(bufferInfo.bufferName()) << channelEncode(bufferInfo.bufferName(), msg);
  emit putCmd("PART", params);
}

// TODO: implement queries
void UserInputHandler::handleQuery(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  QString target = msg.section(' ', 0, 0);
  QString message = msg.section(' ', 1);
  if(message.isEmpty())
    emit displayMsg(Message::Server, BufferInfo::QueryBuffer, target, "Starting query with " + target, network()->myNick(), Message::Self);
  else
    emit displayMsg(Message::Plain, BufferInfo::QueryBuffer, target, message, network()->myNick(), Message::Self);
  handleMsg(bufferInfo, msg);
}

void UserInputHandler::handleQuit(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  emit putCmd("QUIT", serverEncode(msg));
}

void UserInputHandler::handleQuote(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  emit putRawLine(serverEncode(msg));
}

void UserInputHandler::handleSay(const BufferInfo &bufferInfo, const QString &msg) {
  if(bufferInfo.bufferName().isEmpty()) return;  // server buffer
  QList<QByteArray> params;
  params << serverEncode(bufferInfo.bufferName()) << channelEncode(bufferInfo.bufferName(), msg);
  emit putCmd("PRIVMSG", params);
  emit displayMsg(Message::Plain, bufferInfo.type(), bufferInfo.bufferName(), msg, network()->myNick(), Message::Self);
}

void UserInputHandler::handleTopic(const BufferInfo &bufferInfo, const QString &msg) {
  if(bufferInfo.bufferName().isEmpty()) return;
  if(!msg.isEmpty()) {
    QList<QByteArray> params;
    params << serverEncode(bufferInfo.bufferName()) << channelEncode(bufferInfo.bufferName(), msg);
    emit putCmd("TOPIC", params);
  } else {
    emit networkConnection()->putRawLine("TOPIC " + serverEncode(bufferInfo.bufferName()) + " :");
  }
}

void UserInputHandler::handleVoice(const BufferInfo &bufferInfo, const QString &msg) {
  QStringList nicks = msg.split(' ', QString::SkipEmptyParts);
  QString m = "+"; for(int i = 0; i < nicks.count(); i++) m += 'v';
  QStringList params;
  params << bufferInfo.bufferName() << m << nicks;
  emit putCmd("MODE", serverEncode(params));
}

void UserInputHandler::handleWho(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  emit putCmd("WHO", serverEncode(msg.split(' ')));
}

void UserInputHandler::handleWhois(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  emit putCmd("WHOIS", serverEncode(msg.split(' ')));
}

void UserInputHandler::handleWhowas(const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  emit putCmd("WHOWAS", serverEncode(msg.split(' ')));
}

void UserInputHandler::defaultHandler(QString cmd, const BufferInfo &bufferInfo, const QString &msg) {
  Q_UNUSED(bufferInfo)
  emit displayMsg(Message::Error, BufferInfo::StatusBuffer, "", QString("Error: %1 %2").arg(cmd).arg(msg));
}



/*************************************************************************
*                                                                         
* Copyright (C) 2012-2016 Barbara Geller & Ansel Sermersheim                                                         
* All rights reserved.                                                    
*                                                                         
*                                                                         
* GNU Free Documentation License                                          
* This file may be used under the terms of the GNU Free Documentation     
* License version 1.3 as published by the Free Software Foundation        
* and appearing in the file included in the packaging of this file.       
*                                                                         
*                                                                         
*************************************************************************/

#ifndef QSSL_H
#define QSSL_H

QT_BEGIN_NAMESPACE

namespace QSsl {

   enum KeyType {
      PrivateKey,
      PublicKey
   };

   enum KeyAlgorithm {
      Rsa,
      Dsa
   };

   void someFunction(QString data);
};

QT_END_NAMESPACE

#endif

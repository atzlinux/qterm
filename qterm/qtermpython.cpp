#include "qterm.h"

#ifdef HAVE_PYTHON
#include <Python.h>

#include <qapplication.h>
#include <qdir.h>
#include <qfile.h>

#include "qtermwindow.h"
#include "qtermbuffer.h"
#include "qtermtextline.h"
#include "qtermtelnet.h"
#include "qtermparam.h"
#include "qtermbbs.h"

/* **************************************************************************
 *
 *				Pythons Embedding
 *
 * ***************************************************************************/
extern QString pathCfg;

QString getException()
{
	PyObject *pType=NULL, *pValue=NULL, *pTb=NULL, *pName, *pTraceback;

    PyErr_Fetch(&pType, &pValue, &pTb);

    pName = PyString_FromString("traceback");
    pTraceback = PyImport_Import(pName);
    Py_DECREF(pName);
	
	if(pTraceback==NULL)
		return "General Error in Python Callback";
    pName = PyString_FromString("format_exception");
    PyObject *pRes = PyObject_CallMethodObjArgs(pTraceback, pName,pType,pValue,pTb,NULL);
    Py_DECREF(pName);
	
	Py_DECREF(pTraceback);

    Py_XDECREF(pType);
    Py_XDECREF(pValue);
    Py_XDECREF(pTb);

	if(pRes==NULL)
		return "General Error in Python Callback";
	
    pName = PyString_FromString("string");
    PyObject *pString = PyImport_Import(pName);
    Py_DECREF(pName);

	if(pString==NULL)
		return "General Error in Python Callback";

    pName = PyString_FromString("join");
    PyObject *pErr = PyObject_CallMethodObjArgs(pString, pName, pRes,NULL);
    Py_DECREF(pName);

    Py_DECREF(pString);
    Py_DECREF(pRes);

	if(pErr==NULL)
		return "General Error in Python Callback";

    QString str(PyString_AsString(pErr));
	Py_DECREF(pErr);

	return str;
}

QString getErrOutputFile(QTermWindow* lp)
{
	// file name
	QString str2;
	str2.setNum(long(lp));
	str2 += ".err";
	// path
	return pathCfg+str2;
}

// copy current artcle
static PyObject *qterm_copyArticle(PyObject *, PyObject *args)
{
	long lp;
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;

	QTermWindow *pWin=(QTermWindow*)lp;

	QCString cstrArticle;
    QCString cstrCurrent0,cstrCurrent1;
    QCString cstrTemp;

    int pos0=-1,pos1=-1;

    cstrArticle = pWin->stripWhitespace(pWin->m_pBuffer->screen(0)->getText());
    #if defined(_OS_WIN32_) || defined(Q_OS_WIN32)
    cstrArticle += '\r';
    #endif
    cstrArticle += '\n';
	
    while(1)
    {
        cstrCurrent0=pWin->stripWhitespace(pWin->m_pBuffer->screen(0)->getText());
        cstrCurrent1=pWin->stripWhitespace(pWin->m_pBuffer->screen(1)->getText());

        cstrTemp = cstrCurrent0;
        #if defined(_OS_WIN32_) || defined(Q_OS_WIN32)
        cstrTemp += '\r';
        #endif
        cstrTemp +='\n';
        cstrTemp +=cstrCurrent1;

        pos0=cstrArticle.findRev(cstrTemp);

        if(pos0!=-1)
        {
            pos1=cstrArticle.find(cstrCurrent1,pos0);
            if(pos1!=-1)
                cstrArticle.truncate(pos1);
        }

        for(int i=1;i<pWin->m_pBuffer->line()-1;i++)
        {
            cstrArticle+=pWin->stripWhitespace(pWin->m_pBuffer->screen(i)->getText());
            #if defined(_OS_WIN32_) || defined(Q_OS_WIN32)
            cstrArticle += '\r';
            #endif
            cstrArticle+='\n';
        }

        if( pWin->m_pBuffer->screen(pWin->m_pBuffer->line()-1)->getText().find("%") == -1 )
            break;
        pWin->m_pTelnet->write(" ", 1);
		
		if(!pWin->m_wcWaiting.wait(10000))	// timeout
			break;
    }

	PyObject *py_text = PyString_FromString(cstrArticle);

	Py_INCREF(py_text);
	return py_text;
}

static PyObject *qterm_formatError(PyObject *, PyObject *args)
{
	long lp;
	
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;

	QString strErr;
	QString filename = getErrOutputFile((QTermWindow*)lp);

	QDir d;
	if(d.exists(filename))
	{
		QFile file(filename);
		file.open(IO_ReadOnly);
		QTextStream is( &file );
        while ( !is.atEnd() ) 
		{
			strErr += is.readLine(); // line of text excluding '\n'
			strErr += '\n'; 
		}
		file.close();
		d.remove( filename );
	}

	if( !strErr.isEmpty() )
	{
		((QTermWindow*)lp)->m_strPythonError = strErr;
		qApp->postEvent( (QTermWindow*)lp, new QCustomEvent(PYE_ERROR));
	}
	else
		qApp->postEvent( (QTermWindow*)lp, new QCustomEvent(PYE_FINISH));


	Py_INCREF(Py_None);
	return Py_None;
}

// caret x
static PyObject *qterm_caretX(PyObject *, PyObject *args)
{
	long lp;
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;
	int x = ((QTermWindow*)lp)->m_pBuffer->caret().x();
	PyObject * py_x =Py_BuildValue("i",x);
	Py_INCREF(py_x);
	return py_x;
}

// caret y
static PyObject *qterm_caretY(PyObject *, PyObject *args)
{
	long lp;
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;
	int y = ((QTermWindow*)lp)->m_pBuffer->caret().y();
	PyObject * py_y =Py_BuildValue("i",y);
	Py_INCREF(py_y);
	return py_y;

}

// columns
static PyObject *qterm_columns(PyObject *, PyObject *args)
{
	long lp;
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;

	int columns = ((QTermWindow*)lp)->m_pBuffer->columns();
	PyObject * py_columns = Py_BuildValue("i",columns);
	
	Py_INCREF(py_columns);
	return py_columns;

}

// rows
static PyObject *qterm_rows(PyObject *, PyObject *args)
{
	long lp;
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;
	
	int rows = ((QTermWindow*)lp)->m_pBuffer->line();
	PyObject *py_rows = Py_BuildValue("i",rows);

	Py_INCREF(py_rows);
	return py_rows;
}

// sned string to server
static PyObject *qterm_sendString(PyObject *, PyObject *args)
{
	char *pstr;
	long lp;
	int len;

	if (!PyArg_ParseTuple(args, "ls", &lp, &pstr))
		return NULL;
	
	len = strlen(pstr);

	((QTermWindow*)lp)->m_pTelnet->write(pstr,len);

	Py_INCREF(Py_None);
	return Py_None;
}

// same as above except parsing string first "\n" "^p" etc
static PyObject *qterm_sendParsedString(PyObject *, PyObject *args)
{
	char *pstr;
	long lp;
	int len;

	if (!PyArg_ParseTuple(args, "ls", &lp, &pstr))
		return NULL;
	len = strlen(pstr);
	
	((QTermWindow*)lp)->sendParsedString(pstr);

	Py_INCREF(Py_None);
	return Py_None;
}

// get text at line
static PyObject *qterm_getText(PyObject *, PyObject *args)
{
	long lp;
	int line;
	if (!PyArg_ParseTuple(args, "li", &lp, &line))
		return NULL;
	QCString cstr = ((QTermWindow*)lp)->m_pBuffer->screen(line)->getText();

	PyObject *py_text = PyString_FromString(cstr);

	Py_INCREF(py_text);
	return py_text;
}

// get text with attributes
static PyObject *qterm_getAttrText(PyObject *, PyObject *args)
{
	long lp;
	int line;
	if (!PyArg_ParseTuple(args, "li", &lp, &line))
		return NULL;

	QCString cstr = ((QTermWindow*)lp)->m_pBuffer->screen(line)->getAttrText();

	PyObject *py_text = PyString_FromString(cstr);

	Py_INCREF(py_text);
	return py_text;
}

// is host connected
static PyObject *qterm_isConnected(PyObject *, PyObject *args)
{
	long lp;
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;
	
	bool connected = ((QTermWindow*)lp)->isConnected();
	PyObject * py_connected =Py_BuildValue("i",connected?1:0);

	Py_INCREF(py_connected);
	return py_connected;
}

// disconnect from host
static PyObject *qterm_disconnect(PyObject *, PyObject *args)
{
	long lp;
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;
	
	((QTermWindow*)lp)->disconnect();
	
	Py_INCREF(Py_None);
	return Py_None;
}

// reconnect to host
static PyObject *qterm_reconnect(PyObject *, PyObject *args)
{
	long lp;
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;
	
	((QTermWindow*)lp)->reconnect();
	
	Py_INCREF(Py_None);
	return Py_None;
}

// bbs encoding 0-GBK 1-BIG5
static PyObject *qterm_getBBSCodec(PyObject *, PyObject *args)
{
	long lp;
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;
	
	PyObject *py_codec = PyString_FromString(
					((QTermWindow*)lp)->m_param.m_nBBSCode==0?"GBK":"Big5");
	Py_INCREF(py_codec);

	return py_codec;
}

// host address
static PyObject *qterm_getAddress(PyObject *, PyObject *args)
{
	long lp;
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;
	
	PyObject *py_addr = PyString_FromString(
					((QTermWindow*)lp)->m_param.m_strAddr.local8Bit());
	Py_INCREF(py_addr);
	return py_addr;
}

// host port number
static PyObject *qterm_getPort(PyObject *, PyObject *args)
{
	long lp;
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;

	PyObject *py_port = Py_BuildValue("i", ((QTermWindow*)lp)->m_param.m_uPort);
	Py_INCREF(py_port);
	return py_port;
}

// connection protocol 0-telnet 1-SSH1 2-SSH2
static PyObject *qterm_getProtocol(PyObject *, PyObject *args)
{
	long lp;
	if (!PyArg_ParseTuple(args, "l", &lp))
		return NULL;

	PyObject *py_port = Py_BuildValue("i", ((QTermWindow*)lp)->m_param.m_nProtocolType);
	Py_INCREF(py_port);
	return py_port;
}

// key to reply msg
static PyObject *qterm_getReplyKey(PyObject *, PyObject *args)
{
	long lp;
	if(!PyArg_ParseTuple(args, "l", &lp))
		return NULL;

	PyObject *py_key = PyString_FromString(((QTermWindow*)lp)->m_param.m_strReplyKey.local8Bit());
	Py_INCREF(py_key);
	return py_key;
}

// url under mouse 
static PyObject *qterm_getURL(PyObject *, PyObject *args)
{
	long lp;
	if(!PyArg_ParseTuple(args, "l", &lp))
		return NULL;

	PyObject *py_url = PyString_FromString( ((QTermWindow*)lp)->m_pBBS->getUrl());
	Py_INCREF(py_url);
	return py_url;
}

// preview image link
static PyObject *qterm_previewImage(PyObject *, PyObject *args)
{
	long lp;
	char *url;
	if (!PyArg_ParseTuple(args, "ls", &lp, &url))
		return NULL;
	
	((QTermWindow*)lp)->getHttpHelper(url,true);
	
	Py_INCREF(Py_None);
	return Py_None;

}

// convert string from UTF8 to specified encoding
static PyObject *qterm_fromUTF8(PyObject *, PyObject *args)
{
	char *str, *enc;
	if (!PyArg_ParseTuple(args, "ss", &str, &enc))
		return NULL;
	QTextCodec *encodec = QTextCodec::codecForName(enc);
	QTextCodec *utf8 = QTextCodec::codecForName("utf8");
	
	PyObject *py_str = PyString_FromString(
					encodec->fromUnicode(utf8->toUnicode(str)));
	Py_INCREF(py_str);
	return py_str;
}

// convert string from specified encoding to UTF8
static PyObject *qterm_toUTF8(PyObject *, PyObject *args)
{
	char *str, *enc;
	if (!PyArg_ParseTuple(args, "ss", &str, &enc))
		return NULL;
	QTextCodec *encodec = QTextCodec::codecForName(enc);
	QTextCodec *utf8 = QTextCodec::codecForName("utf8");
	
	PyObject *py_str = PyString_FromString(
					utf8->fromUnicode(encodec->toUnicode(str)));
	Py_INCREF(py_str);
	return py_str;
}


PyMethodDef qterm_methods[] = {
	{"formatError",		(PyCFunction)qterm_formatError,			METH_VARARGS,	
			"get the traceback info"},

	{"copyArticle",		(PyCFunction)qterm_copyArticle,			METH_VARARGS,
			"copy current article"},

	{"getText",			(PyCFunction)qterm_getText,				METH_VARARGS,
			"get text at line#"},

	{"getAttrText",		(PyCFunction)qterm_getAttrText,			METH_VARARGS,
			"get attr text at line#"},

	{"sendString",		(PyCFunction)qterm_sendString,			METH_VARARGS,
			"send string to server"},
	
	{"sendParsedString",(PyCFunction)qterm_sendParsedString,	METH_VARARGS,
			"send string with escape"},

	{"caretX",			(PyCFunction)qterm_caretX,				METH_VARARGS,
			"caret x"},
	
	{"caretY",			(PyCFunction)qterm_caretY,				METH_VARARGS,
			"caret y"},

	{"columns",			(PyCFunction)qterm_columns,				METH_VARARGS,
			"screen width"},
	
	{"rows",			(PyCFunction)qterm_rows,				METH_VARARGS,
			"screen height"},
	
	{"isConnected",		(PyCFunction)qterm_isConnected,			METH_VARARGS,
			"connected to server or not"},
	
	{"disconnect",		(PyCFunction)qterm_disconnect,			METH_VARARGS,
			"disconnect from server"},
	
	{"reconnect",		(PyCFunction)qterm_reconnect,			METH_VARARGS,
			"reconnect"},

	{"getBBSCodec",		(PyCFunction)qterm_getBBSCodec,			METH_VARARGS,
			"get the bbs encoding, GBK or Big5"},
	
	{"getAddress",		(PyCFunction)qterm_getAddress,			METH_VARARGS,
			"get the bbs address"},

	{"getPort",			(PyCFunction)qterm_getPort,				METH_VARARGS,
			"get the bbs port number"},

	{"getProtocol",		(PyCFunction)qterm_getPort,				METH_VARARGS,
			"get the bbs protocol, 0/1/2 TELNET/SSH1/SSH2"},
	
	{"getReplyKey",		(PyCFunction)qterm_getReplyKey,			METH_VARARGS,
			"get the key to reply messages"},

	{"getURL",			(PyCFunction)qterm_getURL,				METH_VARARGS,
			"get the url string under mouse"},

	{"previewImage",	(PyCFunction)qterm_previewImage,		METH_VARARGS,
			"preview the image link"},

	{"fromUTF8",		(PyCFunction)qterm_fromUTF8,			METH_VARARGS,
			"decode from utf8 to string in specified codec"},
	
	{"toUTF8",			(PyCFunction)qterm_toUTF8,				METH_VARARGS,
			"decode from string in specified codec to utf8"},

	{NULL,	 			(PyCFunction)NULL, 						0, 				NULL}
};
#endif //HAVE_PYTHON


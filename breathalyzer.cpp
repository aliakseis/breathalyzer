#include <iostream>
#include <fstream>
#include <exception>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <iterator>

#include <climits>

#include <stdio.h>

//#include <assert.h>
#define assert(x)	((void)0)

#include <fcntl.h>
#include <string.h>

#include <omp.h>

#ifdef _WIN32
#include <io.h>
#define open _open
#define lseek _lseek
#define read _read
#define close _close
#else
#undef _O_BINARY
#define _O_BINARY 0
#endif

#ifdef _MSC_VER
#define __inline__ __forceinline
#else
#define _fgetc_nolock fgetc_unlocked
//#define _fgetc_nolock fgetc
#endif

using std::cerr;
using std::cout;
using std::ifstream;
using std::exception;
using std::string;
using std::getline;
using std::vector;
using std::map;
using std::min;
using std::max;
using std::istream_iterator;



static const signed char LogTable256[256] =
{
#define LT(n) n, n, n, n, n, n, n, n, n, n, n, n, n, n, n, n
    -1, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3,
    LT(4), LT(5), LT(5), LT(6), LT(6), LT(6), LT(6),
    LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7), LT(7)
};


inline int Log(unsigned int v) // 32-bit word to find the log of
{
    int r;     // r will be lg(v)
    unsigned int t, tt; // temporaries

    if (tt = v >> 16)
    {
      r = (t = tt >> 8) ? 24 + LogTable256[t] : 16 + LogTable256[tt];
    }
    else
    {
      r = (t = v >> 8) ? 8 + LogTable256[t] : LogTable256[v];
    }

    return r;
}


struct Plex     // Similar to MFC CPlex
// warning variable length structure
{
    Plex* pNext;
    int dwReserved[1];    // align on 8 byte boundary

    void* data() { return this+1; }

    // like 'calloc' but no zero fill
    // may throw memory exceptions
    static Plex* Create(Plex*& pHead, size_t nMax, size_t cbElement)
    {
        assert(nMax > 0 && cbElement > 0);
        Plex* p = (Plex*) operator new(sizeof(Plex) + nMax * cbElement);
                // may throw exception
        p->pNext = pHead;
        pHead = p;  // change head (adds in reverse order for simplicity)
        return p;
    }

    void FreeDataChain()       // free this one and links
    {
        Plex* p = this;
        while (p != 0)
        {
            void* bytes = p;
            p = p->pNext;
            operator delete(bytes);
        }
    }
};

typedef unsigned int UINT;

class FixedAlloc    // Similar to MFC CFixedAlloc
{
// Constructors
public:
    FixedAlloc(UINT nAllocSize, UINT nBlockSize = 64);

// Attributes
    UINT GetAllocSize() { return m_nAllocSize; }

// Operations
public:
    void* Alloc();  // return a chunk of memory of nAllocSize
    void Free(void* p); // free chunk of memory returned from Alloc
    void FreeAll(); // free everything allocated from this allocator

// Implementation
public:
    ~FixedAlloc();

protected:
    struct CNode
    {
        CNode* pNext;	// only valid when in free list
    };

    UINT m_nAllocSize;	// size of each block from Alloc
    UINT m_nBlockSize;	// number of blocks to get at a time
    Plex* m_pBlocks;	// linked list of blocks (is nBlocks*nAllocSize)
    CNode* m_pNodeFree;	// first free node (0 if no free nodes)
};

FixedAlloc::FixedAlloc(UINT nAllocSize, UINT nBlockSize)
{
    assert(nAllocSize >= sizeof(CNode));
    assert(nBlockSize > 1);

    if (nAllocSize < sizeof(CNode))
        nAllocSize = sizeof(CNode);
    if (nBlockSize <= 1)
        nBlockSize = 64;

    m_nAllocSize = nAllocSize;
    m_nBlockSize = nBlockSize;
    m_pNodeFree = 0;
    m_pBlocks = 0;
}

FixedAlloc::~FixedAlloc()
{
    FreeAll();
}

void FixedAlloc::FreeAll()
{
    m_pBlocks->FreeDataChain();
    m_pBlocks = 0;
    m_pNodeFree = 0;
}

void* FixedAlloc::Alloc()
{
    if (m_pNodeFree == 0)
    {
        // add another block
        Plex* pNewBlock = Plex::Create(m_pBlocks, m_nBlockSize, m_nAllocSize);

        // chain them into free list
        // free in reverse order to make it easier to debug
        char* pData = (char*) pNewBlock->data() + m_nAllocSize * (m_nBlockSize - 1);
        for (int i = m_nBlockSize; i > 0; i--, pData -= m_nAllocSize)
        {
            CNode* pNode = (CNode*) pData;
            pNode->pNext = m_pNodeFree;
            m_pNodeFree = pNode;
        }
    }
    assert(m_pNodeFree != 0);  // we must have something

    // remove the first available node from the free list
    void* pNode = m_pNodeFree;
    m_pNodeFree = m_pNodeFree->pNext;
    return pNode;
}

void FixedAlloc::Free(void* p)
{
    if (p != 0)
    {
        // simply return the node to the free list
        CNode* pNode = (CNode*)p;
        pNode->pNext = m_pNodeFree;
        m_pNodeFree = pNode;
    }
}

// http://www.drdobbs.com/184410528

struct Tnode
{
   char splitchar;
   /*bool*/ char terminating;
   unsigned char maxLength, minLength;

   Tnode *eqkid, *hikid;

   Tnode(char ch)
       : splitchar(ch)
       , terminating(false)
       , maxLength(0)
       , minLength(255)
       , eqkid(0), hikid(0)
   {
   }

    void* operator new(size_t size)
    {
        assert(size == s_alloc.GetAllocSize());
        return s_alloc.Alloc();
    }
    void* operator new(size_t, void* p) { return p; }
    void operator delete(void* p) { s_alloc.Free(p); }

protected:
    static FixedAlloc s_alloc;
} ;


FixedAlloc Tnode::s_alloc(sizeof(Tnode), 64);

inline char ToLowerCase(char ch) { return ch | 0x20; }


Tnode* insert_new(FILE *stream, int ch, unsigned int& length)
{
    assert(ch & ~31);

    ch = ToLowerCase(ch);

    Tnode *p = new Tnode(ch);

    ch = _fgetc_nolock( stream );
    if (0 == (ch & ~31) || EOF == ch)
        p->terminating = true;
    else
    {
        ++length;
        p->eqkid = insert_new(stream, ch, length);
    }

    p->maxLength = length;
    p->minLength = length;

    return p;
}


Tnode* insert(Tnode *p, FILE *stream, int ch, unsigned int& length)
{
    assert(ch & ~31);

    ch = ToLowerCase(ch);

    if (p == 0) {
        p = new Tnode(ch);
    }

    assert(ch >= p->splitchar);
    if (ch == p->splitchar) {
        ch = _fgetc_nolock( stream );
        if (0 == (ch & ~31) || EOF == ch)
            p->terminating = true;
        else
        {
            ++length;
            p->eqkid = insert(p->eqkid, stream, ch, length);
        }
    } else
    {
        Tnode* temp = insert_new(stream, ch, length);
        assert(0 == temp->hikid);
        temp->hikid = p;
        temp->minLength = p->minLength;
        temp->maxLength = p->maxLength;
        p = temp;
    }

    if (p->maxLength < length)
        p->maxLength = length;

    if (p->minLength > length)
        p->minLength = length;

    assert(p->minLength <= p->maxLength);

    return p;
}


const char seps[] = "\r\n";

const unsigned int ZERO_DISTANCE = 0xFFFFFFFF;//(unsigned int)-1;

class Breathalyzer
{
private:
    enum { DISTANCE_THRESHOLD = 3 };

    Tnode* wordList;

    unsigned int minLength;// = INT_MAX;

    unsigned int wordDim;

    __inline__
    bool DoSearch(const Tnode* p, unsigned int* d, const string& s,
                  const int offset, const unsigned int maxDistance, unsigned int& distance)
    {
        int threshold = int(s.size());
        if (p->eqkid != 0)
            threshold -= p->eqkid->maxLength - offset;

        unsigned int* upLeft = d;
        unsigned int* downRight = upLeft + s.size() + 2;

        const char* i = s.data();
        const char* dataEnd = i + s.size();

        const char splitchar = p->splitchar;

        const unsigned int startDistance = distance;

        bool ok = (0 < threshold)? ((startDistance >> threshold) >= maxDistance) : (startDistance >= maxDistance);

        if (!ok)
        {
            const char* pThreshold = i + threshold - 1;

            for (; i < pThreshold; ++i, ++upLeft, ++downRight)
            {
                if (*i == splitchar)
                    distance = *upLeft;
                else
                    distance = (*upLeft | *(upLeft + 1) | distance) >> 1;

                *downRight = distance;

                if ((distance >> (pThreshold - i)) >= maxDistance)
                {
                    goto ok_loop;
                }
            }
            for (;;)
            {
                if (*i == splitchar)
                    distance = *upLeft;
                else
                    distance = (*upLeft | *(upLeft + 1) | distance) >> 1;

                *downRight = distance;

                if (distance >= maxDistance)
                {
                    goto ok_loop;
                }

                if (++i >= dataEnd)
                    return false;

                ++upLeft;
                ++downRight;
            }

            return false;
        }
        else
        {
            for (;;)
            {
                if (*i == splitchar)
                    distance = *upLeft;
                else
                    distance = (*upLeft | *(upLeft + 1) | distance) >> 1;

                *downRight = distance;
ok_loop:
                if (++i >= dataEnd)
                    return true;

                ++upLeft;
                ++downRight;
            }

            return true;
        }
    }


    bool GetDistance(const Tnode* p, unsigned int* d, const string& s, int offset, int& maxDistance)
    {
        for (; p != 0; p = p->eqkid, ++offset, d += s.size() + 1)
        {
            if (int(s.length()) - p->maxLength > maxDistance)
                return false;

            if (p->minLength - int(s.length()) > maxDistance)
                return false;

            if (GetDistance(p->hikid, d, s, offset, maxDistance))
                return true;

            if (0 == p->eqkid && !p->terminating)
                return false;

            //int distance(offset);
            unsigned int distance(ZERO_DISTANCE >> offset);
            const unsigned int expMaxDistance(ZERO_DISTANCE >> maxDistance);

            if (!DoSearch(p, d, s, offset, expMaxDistance, distance))
                return false;

            if (p->terminating)
            {
                if (distance > expMaxDistance)
                {
                    maxDistance = 31 - Log(distance);
                    if (maxDistance <= DISTANCE_THRESHOLD)
                        return true;
                }
            }
        }
        return false;
    }

    bool find0(const Tnode* p, const char* s)
    {
        for (; p != 0; p = p->eqkid)
        {
            while (p->splitchar > *s)
            {
                if (0 == p->hikid)
                {
                    return false;
                }
                p = p->hikid;
            }

            if (p->splitchar < *s)
            {
                return false;
            }

            if (0 == *(++s))
            {
                return p->terminating;
            }
        }

        return false;
    }

    bool find(const Tnode* p, unsigned int* d, const string& s, int offset, const int maxDistance)
    {
        for (; p != 0; p = p->eqkid, ++offset, d += s.size() + 1)
        {
            if (int(s.length()) - p->maxLength > maxDistance)
                return false;

            if (p->minLength - int(s.length()) > maxDistance)
                return false;

            if (find(p->hikid, d, s, offset, maxDistance))
                return true;

            if (0 == p->eqkid && !p->terminating)
                return false;

            //int distance(offset);
            unsigned int distance(ZERO_DISTANCE >> offset);
            const unsigned int expMaxDistance(ZERO_DISTANCE >> maxDistance);

            if (!DoSearch(p, d, s, offset, expMaxDistance, distance))
                return false;

            if (p->terminating)
            {
                if (distance >= expMaxDistance)
                    return true;
            }
        }
        return false;
    }

public:
    Breathalyzer() : wordList(0)
    {
    }

    void ReadWordList()
    {
        FILE *stream = fopen("/var/tmp/twl06.txt", "r");

        for (;;)
        {
            int ch;
            do
            {
                ch = _fgetc_nolock( stream );
            }
            while (0 == (ch & ~31));
            if (EOF == ch)
               break;

            unsigned int length(1);
            wordList = insert(wordList, stream, ch, length);
        }

        fclose(stream);

        wordDim = wordList->maxLength + 1;
        minLength = wordList->minLength;
    }

    int GetDistance(const string& s)
    {
        if (find0(wordList, s.c_str()))
            return 0;

        enum { FAST_BUFFER_SIZE = 512 };

        unsigned int sbuf[FAST_BUFFER_SIZE];

        const size_t bufSize = wordDim * (s.size() + 1);
        unsigned int* d = (bufSize > FAST_BUFFER_SIZE)? new unsigned int[bufSize] : sbuf;

        for (unsigned int i = 0; i <= s.size(); ++i)
            d[i] = ZERO_DISTANCE >> i;
        for (unsigned int i = 1; i < wordDim; ++i)
            d[i * (s.size() + 1)] = ZERO_DISTANCE >> i;


        for (int result = 1; result < DISTANCE_THRESHOLD; ++result)
            if (find(wordList, &d[0], s, 1, result))
            {
                if (d != sbuf)
                    delete[] d;
                return result;
            }

        int result = max(minLength, (unsigned int) s.size());
        GetDistance(wordList, &d[0], s, 1, result);

        if (d != sbuf)
            delete[] d;
        return result;
    }
};

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        cerr << "Usage: breathalyzer input_file\n";
        return 1;
    }

    try
    {
        Breathalyzer breathalyzer;

        typedef vector<std::pair<string, int> > Strings;
        Strings strings;

#pragma omp parallel
        {
#pragma omp sections
            {
#pragma omp section
                {
                    breathalyzer.ReadWordList();
                }
#pragma omp section
                {
                    /*
                    string path(argv[1]);
                    if (string::npos == path.find_first_of("\\/"))
                    {
                        string dir(argv[0]);
                        string::size_type pos = dir.find_last_of("\\/");
                        if (string::npos != pos)
                        {
                            path = dir.substr(0, pos + 1) + path;
                        }
                    }

                    ifstream inFile(path.c_str());
                    //*/

                    ifstream inFile(argv[1]);

                    map<string, int> counts;
                    for (istream_iterator<string> iss(inFile), issEnd
                        ; iss != issEnd
                        ; ++iss)
                    {
                        ++counts[*iss];
                    }

                    strings.assign(counts.begin(), counts.end());
                }
            }
        }

        const int numStrings = strings.size();

        int totalDistance = 0;

        int i;

#pragma omp parallel for reduction(+:totalDistance) schedule (dynamic, 10)
        for (i = 0; i < numStrings; ++i)
        {
            Strings::iterator it = strings.begin() + i;
            const int distance = breathalyzer.GetDistance(it->first);
            totalDistance += distance * it->second;
        }

        cout << totalDistance << '\n';
    }
    catch (exception& e)
    {
        cerr << e.what();
        return 1;
    }

    return 0;
}


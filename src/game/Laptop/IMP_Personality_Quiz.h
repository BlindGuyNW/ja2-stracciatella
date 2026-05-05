#ifndef _IMP_PERSOANLITY_QUIZ_H
#define _IMP_PERSOANLITY_QUIZ_H

#include "Types.h"

void EnterIMPPersonalityQuiz( void );
void RenderIMPPersonalityQuiz( void );
void ExitIMPPersonalityQuiz( void );
void HandleIMPPersonalityQuiz( void );

void BltAnswerIndents( INT32 iNumberOfIndents );

// Console_Imp wrappers around the static button callbacks. They preserve
// the same bookkeeping the on-screen Confirm / Prev / Next buttons do
// (advance the question index, compile stats on Q16, run the visited-page
// machinery), so the console doesn't have to mirror it.
void IMP_Quiz_ConfirmAnswer(void);
void IMP_Quiz_NextQuestion(void);
void IMP_Quiz_PrevQuestion(void);

// Number of answers shown for a given question index (0..15). Used by the
// console to bounds-check `imp answer N`.
INT32 IMP_Quiz_AnswerCount(INT32 iQuestion);

// IMP question text record for question N (0..15), pre-resolved for the
// current character's gender. Returns IMP_QUESTION_1 + offset; add +1..+k
// for the answer records.
INT32 IMP_Quiz_QuestionRecord(INT32 iQuestion);

extern INT32 giCurrentPersonalityQuizQuestion;
extern INT32 giMaxPersonalityQuizQuestion;
extern INT32 iCurrentAnswer;
extern INT32 iQuizAnswerList[16];

#endif

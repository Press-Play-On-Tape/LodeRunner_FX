#include "src/utils/Arduboy2Ext.h"
#include <ArduboyTones.h>
#include "src/images/sounds.h"

#include "src/utils/Utils.h"
#include "src/utils/Enums.h"
#include "src/images/Images.h"
#include "src/levels/Levels.h"
#include "src/levels/Level.h"
#include "src/utils/Queue.h"
#include "src/utils/EEPROM_Utils.h"
#include "src/characters/Player.h"
#include "src/characters/Enemy.h"

#include <ArduboyFX.h>
#include "fxdata/fxdata.h" 

Arduboy2Ext arduboy;
ArduboyTones sound(arduboy.audio.enabled);

Player player;
Enemy enemies[NUMBER_OF_ENEMIES];

Level level;


bool flashPlayer = false;

GameState gameState = GameState::SplashScreen_Init;
int8_t bannerStripe = -30;
int8_t introRect = 0;
Queue<Hole, 20> holes;

uint8_t suicide = 0;
uint8_t levelCount = 0;
uint8_t menuSelect = 0;
#ifdef INC_LEVEL_SELECTOR
uint8_t menuLevelSelect = 1;
#endif


// --------------------------------------------------------------------------------------
//  Forward declarations ..
//
uint8_t getNearestX(int8_t margin = HALF_GRID_SIZE);
uint8_t getNearestY(int8_t margin = HALF_GRID_SIZE);


// --------------------------------------------------------------------------------------
//  Setup ..
//
void setup() {

  arduboy.boot();
  arduboy.flashlight();
  arduboy.systemButtons();
  arduboy.setFrameRate(25);
  arduboy.initRandomSeed();
  arduboy.audio.begin();

  EEPROM_Utils::initEEPROM(false);
  EEPROM_Utils::getSavedGameData(&level, &player);

  player.setX(20);
  player.setY(35);
  player.setStance(PlayerStance::StandingStill);

  player.setNextState(GameState::Intro);

  // uint8_t gameNumber = EEPROM_Utils::getGameNumber();

  // if (gameNumber == NUMBER_OF_GAMES) {
  //   gameState = GameState::SeriesOver;
  // }

  FX::disableOLED();      
  FX::begin(FX_DATA_PAGE);

}


// --------------------------------------------------------------------------------------
//  Main Loop ..
//
void loop() {

  if (!(arduboy.nextFrame())) return;
  arduboy.pollButtons();

  switch (gameState) {

    case GameState::SplashScreen_Init:

      splashScreen_Init();
      splashScreen();
      break;

    case GameState::SplashScreen:

      splashScreen();
      break;
      
    case GameState::Intro:
      if (!sound.playing()) sound.tones(score);
      Intro();
      break;

    case GameState::GameSelect:
      GameSelect();
      break;

    case GameState::LevelInit:
      sound.noTone();
      while (!holes.isEmpty()) holes.dequeue();
//      level.setLevelNumber(36);
      level.loadLevel(&player, enemies); 
      introRect = 28;
      gameState = GameState::LevelEntryAnimation;
      /* break; Drop through to next case */

    case GameState::LevelEntryAnimation:
    case GameState::LevelFlash:
    case GameState::LevelPlay:
      LevelPlay();
      break;

    case GameState::LevelExitInit:
      introRect = 0;
      gameState = GameState::LevelExitAnimation;
      /* break; Drop through to next case */

    case GameState::LevelExitAnimation:
    case GameState::GameOver:
    case GameState::RestartLevel:
      LevelPlay();
      break;

    case GameState::NextLevel:
      EEPROM_Utils::saveGameData(&level, &player);
      LevelPlay();
      break;

    case GameState::SeriesOver:
      CompleteSeries();
      break;

    default: break;

  }
  
  FX::enableOLED();             
  arduboy.display(CLEAR_BUFFER);
  FX::disableOLED(); 

}



// --------------------------------------------------------------------------------------
//  Display intro banner ..
//
void Intro() {

  Sprites::drawOverwrite(0, 0, banner, 0);
  if (arduboy.justPressedButtons() & A_BUTTON)  { gameState = GameState::GameSelect; }

}



// --------------------------------------------------------------------------------------
//  Display intro banner ..
//
void GameSelect() {

  bool firstTime = EEPROM_Utils::getMen() == 5 && EEPROM_Utils::getLevelNumber() == 1;

  uint8_t menuOptionY = 24;
  uint8_t selectorY = 24;
  uint8_t const * menuOptionImg = menuOptionStart;

  if (firstTime) {

    selectorY = 24 + (menuSelect * 5);

  }
  else {

    menuOptionY = 19;
    menuOptionImg = menuOption;
    selectorY= 19 + (menuSelect * 10);

  }

  arduboy.drawCompressedMirror(38, menuOptionY, menuOptionImg, WHITE, false);
  arduboy.drawCompressedMirror(31, selectorY, menuArrow, WHITE, false);


  // Brick borders ..

  for (uint8_t x = 0; x < WIDTH + 8; x = x + 16) {
  
    Sprites::drawOverwrite(x - 8, 0, levelSelect, 0);
    Sprites::drawOverwrite(x, 56, levelSelect, 0);

  }


  // Handle buttons ..
  
  uint8_t buttons = arduboy.justPressedButtons();

  if (!firstTime) {

    if ((buttons & UP_BUTTON) && menuSelect > 0)     { menuSelect--; }
    if ((buttons & DOWN_BUTTON) && menuSelect < 1)   { menuSelect++; }

  }

  if (buttons & A_BUTTON) {
    
    if (menuSelect == 0) { EEPROM_Utils::getSavedGameData(&level, &player); gameState = GameState::LevelInit; }
    if (menuSelect == 1) { EEPROM_Utils::initEEPROM(true); EEPROM_Utils::getSavedGameData(&level, &player); gameState = GameState::LevelInit; }
     
  }

}


// --------------------------------------------------------------------------------------
//  Play the current level ..  
//
//  If 'play' is false, play is halted and the player flashes waiting on a keypress.
//
void LevelPlay() {

  uint8_t nearestX = getNearestX();
  uint8_t nearestY = getNearestY();


  if (gameState == GameState::LevelPlay) {

    LevelElement nearest = level.getLevelData(nearestX, nearestY);


    // Detect next movements for player and enemies ..

    playerMovements(nearestX, nearestY, nearest);


    if (arduboy.everyXFrames(2)) {

      clearEnemyMovementPositions(enemies);
      for (uint8_t x = 0; x < NUMBER_OF_ENEMIES; x++) {

        Enemy *enemy = &enemies[x];

        if (enemy->getEnabled()) {

          enemyMovements(enemy);

        }

      }

    }

  }


  // Render the screen ..

  renderScreen();


  // Update the player and enemy stance, positions, etc ..

  if (gameState == GameState::LevelPlay) {



    // Update player stance ..

    if (arduboy.everyXFrames(2)) {

      if ((player.getXDelta() != 0 || player.getYDelta() != 0 || level.getXOffsetDelta() != 0 || level.getYOffsetDelta() != 0)) {

        player.setStance(getNextStance(player.getStance()));

      }
    
    }
    if (arduboy.everyXFrames(4)) {


      // Update enemy stances ..

      for (uint8_t x = 0; x < NUMBER_OF_ENEMIES; x++) {

        Enemy *enemy = &enemies[x];
        PlayerStance stance = enemy->getStance();

        if (enemy->getEnabled() && enemy->getEscapeHole() == EscapeHole::None) {

          switch (stance) {

            case PlayerStance::Rebirth_1 ... PlayerStance::Rebirth_3:
              
              enemy->setStance(getNextStance(stance));
              break;

            default:
              
              if (enemy->getXDelta() != 0 || enemy->getYDelta() != 0) {

                enemy->setStance(getNextStance(stance));

              }

              break;

          }

        }

      }

    }


    // Move player ..

    player.setX(player.getX() + player.getXDelta());
    player.setY(player.getY() + player.getYDelta());
    level.setXOffset(level.getXOffset() + level.getXOffsetDelta());
    level.setYOffset(level.getYOffset() + level.getYOffsetDelta());


    // If the player has gone off the top of the screen .. level over!

    LevelElement current = level.getLevelData(getNearestX(), getNearestY());

    if (player.getY() <= 1 && current == LevelElement::Ladder) {

      uint8_t levelNumber = level.getLevelNumber() + 1;
      player.incrementMen();

      gameState = GameState::LevelExitInit;
      level.setLevelNumber(levelNumber);
      EEPROM_Utils::saveLevelNumber(level.getLevelNumber());

      if (levelNumber > LEVEL_COUNT) {

        player.setNextState(GameState::SeriesOver);

      }
      else {
        player.setNextState(GameState::NextLevel);
      }

      sound.tones(levelComplete); 

    } 


    // Move enemies ..

    if (arduboy.everyXFrames(2)) {

      for (uint8_t x = 0; x < NUMBER_OF_ENEMIES; x++) {

        Enemy *enemy = &enemies[x];

        if (enemy->getEnabled()) {

          enemy->setX(enemy->getX() + enemy->getXDelta());
          enemy->setY(enemy->getY() + enemy->getYDelta());

        }


        // Are any of the enemies touching the player?

        if (enemy->getEnabled() && arduboy.collide(Rect {static_cast<int16_t>(enemy->getX()) + 2, static_cast<int16_t>(enemy->getY()) + 2, 6, 6}, Rect {static_cast<int16_t>(player.getX() - level.getXOffset()) + 2, static_cast<int16_t>(player.getY() - level.getYOffset()) + 2, 6, 6} )) {

          playerDies();

        }

      }


      // Update level details ..
      
      for (uint8_t y = 0; y < level.getHeight(); y++) {

        for (uint8_t x = 0; x < level.getWidth() * 2; x++) {

          LevelElement element = (LevelElement)level.getLevelData(x, y);
          
          switch (element) {

            case LevelElement::Brick_1 ... LevelElement::Brick_4:
              element++;
              level.setLevelData(x, y, element);
              break;

            default:
              break;

          }

        }

      }

    }


    // Do any holes need to be filled in ?

    if (!holes.isEmpty()) {

      for (uint8_t x = 0; x < holes.getCount(); x++) {

        Hole &hole = holes.operator[](x);

        if (hole.countDown > 0) {

          hole.countDown--;

          switch (hole.countDown) {

            case HOLE_FILL_4:        
              level.setLevelData(hole.x, hole.y, LevelElement::Brick_Close_1);
              break;

            case HOLE_FILL_3:
              level.setLevelData(hole.x, hole.y, LevelElement::Brick_Close_2);
              break;

            case HOLE_FILL_2:
              level.setLevelData(hole.x, hole.y, LevelElement::Brick_Close_3);
              break;

            case HOLE_FILL_1:
              level.setLevelData(hole.x, hole.y, LevelElement::Brick_Close_4);
              break;

            case 1:


              // Have any of the enemies been trapped ?  If so, relocate them ..

              for (uint8_t x = 0; x < NUMBER_OF_ENEMIES; x++) {

                Enemy *enemy = &enemies[x];

                if (enemy->getEnabled() && (hole.x * GRID_SIZE) == enemy->getX() && (hole.y * GRID_SIZE) == enemy->getY()) {

                  LevelPoint reentryPoint = level.getNextReentryPoint();
                  enemy->setX(reentryPoint.x * GRID_SIZE);
                  enemy->setY(reentryPoint.y * GRID_SIZE);
                  enemy->setStance( PlayerStance::Rebirth_1);
                  enemy->setEscapeHole(EscapeHole::None);
                  enemy->setXDelta(0);
                  enemy->setYDelta(0);

                }

              }


              // What about the player ?

              if ( hole.x == nearestX && hole.y == nearestY ) {

                  playerDies();

              }

              level.setLevelData(hole.x, hole.y, LevelElement::Brick);
              break;

            default: break;

          }

        }

      }


      // Burn any holes that have been filled in from the queue ..

      while (true) {
        
        Hole &hole = holes.peek();

        if (hole.countDown == 1) {

          holes.dequeue();

          if (holes.isEmpty()) { break; }

        }
        else {

          break;

        }

      }

    }

  }
  else {

    uint8_t justPressed = arduboy.justPressedButtons();
    uint8_t pressed = arduboy.pressedButtons();


    // Change level?

    #ifdef CHANGE_LEVELS
    if (gameState == GameState::LevelFlash) {

      if (pressed & B_BUTTON) {
  
        //if (arduboy.everyXFrames(2)) {

          switch (levelCount) {

            case 0 ... 40:
            
              levelCount++;
              break;

            case 41 ... 45:

              arduboy.setRGBled(0,0,64);
              levelCount++;
              break;

            case 46 ... 50:

              arduboy.setRGBled(0,0,0);
              levelCount++;
              break;
                    
            default:

              uint8_t levelNumber = level.getLevelNumber();

              if (justPressed & UP_BUTTON && levelNumber < LEVEL_COUNT) {
                level.setLevelNumber(levelNumber + 1);
              }
              else if ((justPressed & DOWN_BUTTON) && levelNumber > + 1) {
                level.setLevelNumber(levelNumber - 1);
              }

              break;

          }

          justPressed = 0;

      }
      else {

        switch (levelCount) {

          case 0:
            break;

          case 1 ... 50:
            justPressed = 4;
            break;

          default:

            if (levelCount >= 40) {

              gameState = GameState::LevelInit;
              levelCount = 0;

            }
            else {

              levelCount = 0;

            }

            break;

        }

      }

    }
    else {

      levelCount = 0;

    }
    #endif


    // We are not playing so wait for a key press to continue the game ..

    if (justPressed > 0) { 

      switch (gameState) {

        case GameState::NextLevel:
        case GameState::RestartLevel:
          gameState = GameState::LevelInit;  
          break;

        case GameState::GameOver:
          gameState = GameState::Intro;  
          break;

        case GameState::LevelExitAnimation:
          gameState = player.getNextState();
          break;

        default:
          arduboy.clearButtonState();
          gameState = GameState::LevelPlay;
          break;

      }  
      
    }

  }


  // Show level clear indicator?

  if (suicide == 0 && levelCount == 0) {
    arduboy.setRGBled(0, (level.getGoldLeft() == 0 && gameState == GameState::LevelPlay ? 32 : 0), 0);
  }

  //arduboy.display(CLEAR_BUFFER);

}


// --------------------------------------------------------------------------------------
//  Our player is dead ..
//
void playerDies() {

  uint8_t menLeft = player.getMen() - 1;

  player.setMen(menLeft);
  gameState = GameState::LevelExitInit;

  if (menLeft > 0) {

    player.setNextState(GameState::RestartLevel);

  }
  else {

    player.setNextState(GameState::GameOver);

  }

  sound.tones(dead); 

}


// --------------------------------------------------------------------------------------
//  Display 'victory' banner ..
//
void CompleteSeries() {

  arduboy.drawCompressedMirror(29, 24, victory, WHITE, false);
  //arduboy.display(CLEAR_BUFFER);

}

#include <iostream>
#include <string>
#include <memory>
#include <vector>
extern "C" {
    #include "sqlite3.h"
}
#include <windows.h>




//=========================================
// 1. Реализовать контейнер с итератором на основе базы данных SQLite для выбранного семейства объектов.
//=========================================

// ПАТТЕРНЫ И БАЗОВЫЕ КЛАССЫ

// 1. Паттерн "Стратегия"
// Интерфейс для стратегии экспорта данных
class IExportStrategy {
public:
    virtual void exportData(const std::string& title, const std::string& extra) const = 0;
    virtual ~IExportStrategy() = default;
};

// Конкретная стратегия 1: Экспорт в обычный текст
class TextExportStrategy : public IExportStrategy {
public:
    void exportData(const std::string& title, const std::string& extra) const override {
        std::cout << "[TEXT EXPORT] Title: " << title << " | Info: " << extra << "\n";
    }
};

// Конкретная стратегия 2: Экспорт в формат JSON
class JsonExportStrategy : public IExportStrategy {
public:
    void exportData(const std::string& title, const std::string& extra) const override {
        std::cout << "[JSON EXPORT] { \"title\": \"" << title << "\", \"info\": \"" << extra << "\" }\n";
    }
};

// 2. Семейство объектов и паттерн "Шаблонный метод"
class Publication {
protected:
    int id;
    std::string title;
    std::string extra_info; // Автор для книги, номер для журнала
    std::unique_ptr<IExportStrategy> exportStrategy; // Использование умного указателя для стратегии

public:
    Publication(int id, std::string t, std::string extra, std::unique_ptr<IExportStrategy> strategy)
        : id(id), title(std::move(t)), extra_info(std::move(extra)), exportStrategy(std::move(strategy)) {}

    virtual ~Publication() = default;

    // Паттерн "Шаблонный метод"
    // Определяет скелет алгоритма вывода на экран
    void printReport() const {
        printHeader();       // Шаг 1: Общий для всех
        printDetails();      // Шаг 2: Реализуется наследниками (чисто виртуальный)
        printFooter();       // Шаг 3: Общий для всех
    }

    void doExport() const {
        if (exportStrategy) {
            exportStrategy->exportData(title, extra_info);
        }
    }

protected:
    void printHeader() const {
        std::cout << "--- Publication Report (ID: " << id << ") ---\n";
    }

    virtual void printDetails() const = 0; // Примитивная операция

    void printFooter() const {
        std::cout << "----------------------------------\n";
    }
};

// Конкретный класс: Книга
class Book : public Publication {
public:
    Book(int id, std::string t, std::string author, std::unique_ptr<IExportStrategy> strategy)
        : Publication(id, std::move(t), std::move(author), std::move(strategy)) {}

protected:
    void printDetails() const override {
        std::cout << "Type: BOOK\n";
        std::cout << "Title: " << title << "\n";
        std::cout << "Author: " << extra_info << "\n";
    }
};

// Конкретный класс: Журнал
class Magazine : public Publication {
public:
    Magazine(int id, std::string t, std::string issue, std::unique_ptr<IExportStrategy> strategy)
        : Publication(id, std::move(t), std::move(issue), std::move(strategy)) {}

protected:
    void printDetails() const override {
        std::cout << "Type: MAGAZINE\n";
        std::cout << "Title: " << title << "\n";
        std::cout << "Issue No: " << extra_info << "\n";
    }
};






// УМНЫЕ УКАЗАТЕЛИ ДЛЯ SQLITE

// Функторы для автоматического закрытия ресурсов базы данных
struct SQLiteDeleter {
    void operator()(sqlite3* db) { sqlite3_close(db); }
};

struct SQLiteStmtDeleter {
    void operator()(sqlite3_stmt* stmt) { sqlite3_finalize(stmt); }
};

using DbPtr = std::shared_ptr<sqlite3>;
using StmtPtr = std::shared_ptr<sqlite3_stmt>;







// ИТЕРАТОР И КОНТЕЙНЕР БАЗЫ ДАННЫХ

// Итератор, который "шагает" по строкам SQLite
class DbIterator {
private:
    StmtPtr stmt;
    bool is_end;
    std::shared_ptr<Publication> current_obj;

    // Чтение текущей строки из БД и создание объекта
    void fetchCurrent() {
        if (!stmt || is_end) return;

        int stepResult = sqlite3_step(stmt.get());
        if (stepResult == SQLITE_ROW) {
            int id = sqlite3_column_int(stmt.get(), 0);
            std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 1));
            std::string title = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 2));
            std::string extra = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 3));

            // Фабричный метод создания полиморфных объектов прямо в итераторе
            // Назначаем стратегии случайным образом для примера
            if (type == "Book") {
                current_obj = std::make_shared<Book>(id, title, extra, std::make_unique<TextExportStrategy>());
            } else {
                current_obj = std::make_shared<Magazine>(id, title, extra, std::make_unique<JsonExportStrategy>());
            }
        } else {
            is_end = true;
            current_obj = nullptr;
        }
    }

public:
    // Конструктор
    DbIterator(StmtPtr statement, bool end_flag = false) : stmt(statement), is_end(end_flag) {
        if (!is_end) {
            fetchCurrent();
        }
    }

    // Оператор разыменования
    Publication& operator*() { return *current_obj; }
    std::shared_ptr<Publication> operator->() { return current_obj; }

    // Префиксный инкремент
    DbIterator& operator++() {
        fetchCurrent();
        return *this;
    }

    // Сравнение итераторов (нужно для цикла range-based for)
    bool operator!=(const DbIterator& other) const {
        return is_end != other.is_end;
    }
};

// Контейнер, скрывающий внутри себя базу данных SQLite
class SQLiteContainer {
private:
    DbPtr db;

public:
    SQLiteContainer(const std::string& db_name) {
        sqlite3* raw_db = nullptr;
        if (sqlite3_open(db_name.c_str(), &raw_db) != SQLITE_OK) {
            throw std::runtime_error("Can't open database");
        }
        // Оборачиваем "сырой" указатель в умный с кастомным удалителем
        db = DbPtr(raw_db, SQLiteDeleter());
        initDatabase();
    }

    void initDatabase() {
        const char* sql = "CREATE TABLE IF NOT EXISTS publications ("
                          "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                          "type TEXT, title TEXT, extra TEXT);";
        char* errMsg = nullptr;
        sqlite3_exec(db.get(), sql, nullptr, nullptr, &errMsg);
        if (errMsg) {
            sqlite3_free(errMsg);
        }
    }

    void add(const std::string& type, const std::string& title, const std::string& extra) {
        std::string sql = "INSERT INTO publications (type, title, extra) VALUES ('" + 
                          type + "', '" + title + "', '" + extra + "');";
        sqlite3_exec(db.get(), sql.c_str(), nullptr, nullptr, nullptr);
    }

    // Методы для поддержки итератора (begin и end)
    DbIterator begin() {
        sqlite3_stmt* raw_stmt = nullptr;
        const char* sql = "SELECT id, type, title, extra FROM publications;";
        sqlite3_prepare_v2(db.get(), sql, -1, &raw_stmt, nullptr);
        
        StmtPtr stmt(raw_stmt, SQLiteStmtDeleter());
        return DbIterator(stmt, false);
    }

    DbIterator end() {
        return DbIterator(nullptr, true);
    }
};




//==================================================================================
//2. Реализовать взаимодействие с объектами выбранного семейства с использованием паттерна "Стратегия". Интерфейс существующих функций менять запрещено!
//==================================================================================

// ПАТТЕРН "СТРАТЕГИЯ"

// Интерфейс стратегии взаимодействия. 
// Принимает константную ссылку на базовый класс, не требуя изменения его интерфейса.
class IWorkflowStrategy {
public:
    virtual void interact(const Publication& pub) const = 0;
    virtual ~IWorkflowStrategy() = default;
};

// Конкретная стратегия 1: "Полный аудит"
// Взаимодействует с объектом, вызывая полный отчет на экран и экспорт данных.
class FullAuditStrategy : public IWorkflowStrategy {
public:
    void interact(const Publication& pub) const override {
        std::cout << ">>> [AUDIT START]\n";
        pub.printReport(); // Использование существующего метода
        pub.doExport();    // Использование существующего метода
        std::cout << "<<< [AUDIT END]\n\n";
    }
};

// Конкретная стратегия 2: Тихий экспорт
// Взаимодействует с объектом скрытно, вызывая только экспорт данных.
class SilentBackupStrategy : public IWorkflowStrategy {
public:
    void interact(const Publication& pub) const override {
        // Не вызываем printReport(), делаем только экспорт
        std::cout << "[BACKUP SYSTEM] Processing data... ";
        pub.doExport(); 
    }
};



// КОНТЕКСТ ДЛЯ ВЫПОЛНЕНИЯ СТРАТЕГИИ

class LibraryProcessor {
private:
    std::shared_ptr<IWorkflowStrategy> strategy;

public:
    // Установка стратегии в рантайме (динамическое изменение поведения)
    void setStrategy(std::shared_ptr<IWorkflowStrategy> newStrategy) {
        strategy = std::move(newStrategy);
    }

    // Выполнение стратегии над всем контейнером.
    // Обратите внимание: мы используем итератор контейнера из Задания 1, 
    // не меняя логику работы БД.
    void processAll(SQLiteContainer& container) const {
        if (!strategy) {
            std::cerr << "Strategy is not set!\n";
            return;
        }

        for (auto& pub : container) {
            // Делегирование взаимодействия выбранной стратегии
            strategy->interact(pub); 
        }
    }
};




//==================================================================================
//3. Для выбранного семейства объектов реализовать паттерн "Шаблонный метод" (добавит некоторую функцию взаимодейтсвия с объектом, алгоритм которой является общим для всех объектов, но может иметь специфические реализации отдельных этапов для каждого конкретного объекта из семейства).
//==================================================================================

// БАЗОВЫЙ КЛАСС И СКЕЛЕТ АЛГОРИТМА (ШАБЛОННЫЙ МЕТОД)

class Publication2 {
protected:
    int id;
    std::string title;
    std::string extra_info;

public:
    Publication2(int id, std::string t, std::string extra)
        : id(id), title(std::move(t)), extra_info(std::move(extra)) {}

    virtual ~Publication2() = default;

    // ШАБЛОННЫЙ МЕТОД.
    // Функция не виртуальная, чтобы наследники не могли сломать саму последовательность действий.
    void archivePublication() const {
        std::cout << "[НАЧАЛО АРХИВАЦИИ] Объект ID: " << id << " (" << title << ")\n";
        
        checkCondition();         // Шаг 1: Общий для всех (не переопределяется)
        assignMetadata();         // Шаг 2: Уникальный (переопределяется наследниками)
        calculateStorageSpace();  // Шаг 3: Уникальный (переопределяется наследниками)
        saveToArchiveLog();       // Шаг 4: Общий для всех (не переопределяется)
        
        std::cout << "[КОНЕЦ АРХИВАЦИИ]\n\n";
    }

protected:
    // ПРИМИТИВНЫЕ ОПЕРАЦИИ АЛГОРИТМА

    // Базовая реализация
    void checkCondition() const {
        std::cout << "  -> Шаг 1: Проверка физического состояния (Пыль смахнули, страницы целы).\n";
    }

    // Чисто виртуальная функция (Обязательна к реализации в дочерних классах)
    virtual void assignMetadata() const = 0;

    // Виртуальная функция с реализацией по умолчанию
    // Дочерний класс может её переопределить, а может оставить как есть.
    virtual void calculateStorageSpace() const {
        std::cout << "  -> Шаг 3: Выделение стандартного места на полке.\n";
    }

    // Базовая реализация
    void saveToArchiveLog() const {
        std::cout << "  -> Шаг 4: Запись данных об объекте в базу данных архива.\n";
    }
};

// РЕАЛИЗАЦИЯ СПЕЦИФИЧНЫХ ШАГОВ В ДОЧЕРНИХ КЛАССАХ

// Класс "Книга"
class Book2 : public Publication2 {
public:
    Book2(int id, std::string t, std::string author)
        : Publication2(id, std::move(t), std::move(author)) {}

protected:
    // Специфичная реализация Шага 2 для Книги
    void assignMetadata() const override {
        std::cout << "  -> Шаг 2 [КНИГА]: Генерация архивных тегов по Автору ('" 
                  << extra_info << "') и присвоение ISBN.\n";
    }

    // Специфичная реализация Шага 3 для Книги
    void calculateStorageSpace() const override {
        std::cout << "  -> Шаг 3 [КНИГА]: Бронирование места в секции долгосрочного хранения (Тяжелые тома).\n";
    }
};

// Класс "Журнал" 
class Magazine2 : public Publication2 {
public:
    Magazine2(int id, std::string t, std::string issue)
        : Publication2(id, std::move(t), std::move(issue)) {}

protected:
    // Специфичная реализация Шага 2 для Журнала
    void assignMetadata() const override {
        std::cout << "  -> Шаг 2 [ЖУРНАЛ]: Генерация тегов по Номеру выпуска ('" 
                  << extra_info << "') и присвоение ISSN.\n";
    }

    // Специфичная реализация Шага 3 для Журнала
    void calculateStorageSpace() const override {
        std::cout << "  -> Шаг 3 [ЖУРНАЛ]: Размещение в общую папку-скоросшиватель за текущий год.\n";
    }
};




// ГЛАВНАЯ ФУНКЦИЯ

int main() {
    SetConsoleOutputCP(CP_UTF8); 
    SetConsoleCP(CP_UTF8);
    SQLiteContainer container("library.db");    

    // //==================================================================================
    // // 1. Реализовать контейнер с итератором на основе базы данных SQLite для выбранного семейства объектов.
    // //==================================================================================

    // try {
    //     std::cout << "[SYSTEM] Инициализация базы данных...\n";
    //     // Для демонстрации добавим записи (в реальности они сохранятся в файле library.db)
    //     container.add("Book", "The C++ Programming Language", "Bjarne Stroustrup");
    //     container.add("Magazine", "Linux Format", "Issue 250");
    //     container.add("Book", "Design Patterns", "GoF");

    //     std::cout << "[SYSTEM] Чтение через итератор БД:\n\n";

    //     // Использование реализованного итератора в range-based for цикле
    //     for (auto& pub : container) {
    //         // Вызов Шаблонного метода
    //         pub.printReport(); 
            
    //         // Вызов метода, использующего паттерн Стратегия
    //         pub.doExport();    
            
    //         std::cout << "\n";
    //     }
    // }
    // catch (const std::exception& e) {
    //     std::cerr << "Ошибка: " << e.what() << "\n";
    // }

    //==================================================================================
    //2. Реализовать взаимодействие с объектами выбранного семейства с использованием паттерна "Стратегия". Интерфейс существующих функций менять запрещено!
    //==================================================================================

    // try {
    //     // Если БД пустая (при первом запуске), можно добавить пару записей
    //     container.add("Book", "Clean Code", "Robert Martin");
    //     container.add("Magazine", "Wired", "Issue 12");

    //     // 2. Создание процессора
    //     LibraryProcessor processor;

    //     std::cout << "============================================\n";
    //     std::cout << "СЦЕНАРИЙ 1: ПРИМЕНЕНИЕ СТРАТЕГИИ ПОЛНОГО АУДИТА\n";
    //     std::cout << "============================================\n";
        
    //     // Устанавливаем стратегию аудита
    //     processor.setStrategy(std::make_shared<FullAuditStrategy>());
    //     processor.processAll(container);

    //     std::cout << "\n============================================\n";
    //     std::cout << "СЦЕНАРИЙ 2: ПРИМЕНЕНИЕ СТРАТЕГИИ ТИХОГО БЭКАПА\n";
    //     std::cout << "============================================\n";
        
    //     // Динамически меняем стратегию на тихий бэкап в процессе выполнения
    //     processor.setStrategy(std::make_shared<SilentBackupStrategy>());
    //     processor.processAll(container);

    // }
    // catch (const std::exception& e) {
    //     std::cerr << "Ошибка: " << e.what() << "\n";
    // }


    //==================================================================================
    //3. Для выбранного семейства объектов реализовать паттерн "Шаблонный метод" (добавит некоторую функцию взаимодейтсвия с объектом, алгоритм которой является общим для всех объектов, но может иметь специфические реализации отдельных этапов для каждого конкретного объекта из семейства).
    //==================================================================================

    std::vector<std::unique_ptr<Publication2>> library;

    // Добавляем объекты семейства в контейнер
    library.push_back(std::make_unique<Book2>(101, "Гарри Поттер", "Дж.К. Роулинг"));
    library.push_back(std::make_unique<Magazine2>(202, "Наука и Жизнь", "Выпуск №9"));
    library.push_back(std::make_unique<Book2>(103, "Изучаем C++", "Б. Страуструп"));

    std::cout << "=== ЗАПУСК ПАКЕТНОЙ АРХИВАЦИИ БИБЛИОТЕКИ ===\n\n";

    // Итерация по объектам
    for (const auto& pub : library) {
        // Вызов Шаблонного метода.
        // Алгоритм не знает, Книга это или Журнал.
        // Просто запускается общий процесс архивации.
        pub->archivePublication();
    }

    return 0;
}